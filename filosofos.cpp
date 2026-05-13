#include <iostream>
#include <vector>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <iomanip>
#include <random>
#include <atomic>

using namespace std;

// Estados do filósofo
enum Estado { PENS, FOME, COME };

// Estrutura para os dados da simulação
struct Simulacao {
    int N;
    int duracao_total;
    int t_pensar_min, t_pensar_max;
    int t_comer_min, t_comer_max;
    struct timeval start_time;
    atomic<bool> ativo;

    vector<pthread_mutex_t> garfos_mutex;
    vector<bool> garfos_estado;
    vector<Estado> filosofos_estado;
    vector<int> refeicoes;
    pthread_mutex_t print_mutex; // Agora atua como um "estado_global_mutex"
};

struct DadosFilosofo {
    int id;
    Simulacao* sim;
};

// Função para formatar o tempo relativo [HH:MM:SS.mmm]
string get_timestamp(struct timeval start) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long millis = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
    
    int hh = millis / 3600000;
    int mm = (millis % 3600000) / 60000;
    int ss = (millis % 60000) / 1000;
    int mmm = millis % 1000;

    char buf[20];
    sprintf(buf, "[%02d:%02d:%02d.%03d]", hh, mm, ss, mmm);
    return string(buf);
}

// Função para imprimir o evento. 
// ATENÇÃO: Ela não tem mais mutex dentro dela. O mutex deve ser chamado ANTES de mudar o estado.
void imprimir_evento(Simulacao* sim, int id, string de, string para) {
    cout << get_timestamp(sim->start_time) << " F" << id << ": " << de << " > " << para << endl;
    
    cout << "Garfos: ";
    for(int i = 0; i < sim->N; ++i) 
        cout << "[" << (sim->garfos_estado[i] ? "X" : "O") << "]" << (i == sim->N - 1 ? "" : " ");
    cout << endl;

    cout << "Filósofos: ";
    for(int i = 0; i < sim->N; ++i) {
        string st = (sim->filosofos_estado[i] == PENS) ? "PENS" : (sim->filosofos_estado[i] == FOME ? "FOME" : "COME");
        cout << "F" << i << ":" << st << (i == sim->N - 1 ? "" : " | ");
    }
    cout << endl;

    cout << "Refeições: ";
    for(int i = 0; i < sim->N; ++i)
        cout << "F" << i << ":" << sim->refeicoes[i] << (i == sim->N - 1 ? "" : " | ");
    cout << "\n------------------------------------------------------------" << endl;
}

// Thread do Filósofo
void* rotina_filosofo(void* arg) {
    DadosFilosofo* dados = (DadosFilosofo*)arg;
    Simulacao* sim = dados->sim;
    int id = dados->id;

    // Gerador de números aleatórios único por thread
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist_pensar(sim->t_pensar_min, sim->t_pensar_max);
    uniform_int_distribution<> dist_comer(sim->t_comer_min, sim->t_comer_max);

    // Definição dos garfos (esquerda e direita)
    int g_esq = id;
    int g_dir = (id + 1) % sim->N;

    // Estratégia de Hierarquia: sempre pegar o menor índice primeiro para evitar Deadlock
    int primeiro = (g_esq < g_dir) ? g_esq : g_dir;
    int segundo  = (g_esq < g_dir) ? g_dir : g_esq;

    while (sim->ativo) {
        // TEMPO PENSANDO
        // A thread apenas dorme. O estado já é inicializado como PENS no main ou no final do loop
        usleep(dist_pensar(gen) * 1000);

        if (!sim->ativo) break; // Sai limpo se o tempo acabou enquanto pensava

        // ESTADO: FOME
        // Trava para mudar o estado e imprimir ao mesmo tempo
        pthread_mutex_lock(&sim->print_mutex);
        sim->filosofos_estado[id] = FOME;
        imprimir_evento(sim, id, "PENS", "FOME");
        pthread_mutex_unlock(&sim->print_mutex);

        // Tentar pegar garfos físicos (Isso fica FORA do print_mutex para não travar os outros que querem imprimir)
        pthread_mutex_lock(&sim->garfos_mutex[primeiro]);
        pthread_mutex_lock(&sim->garfos_mutex[segundo]);

        // ESTADO: COMENDO
        // Pegou os garfos! Trava a tela, muda todos os estados e imprime atômicamente
        pthread_mutex_lock(&sim->print_mutex);
        sim->garfos_estado[primeiro] = true;
        sim->garfos_estado[segundo] = true;
        sim->filosofos_estado[id] = COME;
        sim->refeicoes[id]++;
        imprimir_evento(sim, id, "FOME", "COME");
        pthread_mutex_unlock(&sim->print_mutex);
        
        // TEMPO COMENDO
        usleep(dist_comer(gen) * 1000);

        // ESTADO: PENSANDO (Largando os garfos)
        // Trava a tela, limpa o estado dos garfos, muda para PENS e imprime atômicamente
        pthread_mutex_lock(&sim->print_mutex);
        sim->garfos_estado[segundo] = false;
        sim->garfos_estado[primeiro] = false;
        sim->filosofos_estado[id] = PENS;
        imprimir_evento(sim, id, "COME", "PENS");
        pthread_mutex_unlock(&sim->print_mutex);

        // DEVOLVE OS GARFOS FÍSICOS (Destrava os mutexes dos garfos para os vizinhos)
        pthread_mutex_unlock(&sim->garfos_mutex[segundo]);
        pthread_mutex_unlock(&sim->garfos_mutex[primeiro]);
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    Simulacao sim;

    // Verificação ajustada para 7 parâmetros (Nome + 6 args)
    if (argc < 7) {
        cout << "Uso: " << argv[0] << " N DURACAO PENS_MIN PENS_MAX COMER_MIN COMER_MAX" << endl;
        return 1;
    }

    sim.N = stoi(argv[1]);
    
    // Validação extra exigida no PDF (N >= 3)
    if (sim.N < 3) {
        cout << "Erro: O número de filósofos (N) deve ser pelo menos 3." << endl;
        return 1;
    }

    sim.duracao_total = stoi(argv[2]);
    sim.t_pensar_min = stoi(argv[3]);
    sim.t_pensar_max = stoi(argv[4]);
    sim.t_comer_min = stoi(argv[5]);
    sim.t_comer_max = stoi(argv[6]);
    sim.ativo = true;

    // Inicialização de vetores e mutexes
    sim.garfos_mutex.resize(sim.N);
    sim.garfos_estado.assign(sim.N, false);
    sim.filosofos_estado.assign(sim.N, PENS); // Todos começam pensando
    sim.refeicoes.assign(sim.N, 0);
    pthread_mutex_init(&sim.print_mutex, NULL);

    for (int i = 0; i < sim.N; i++) {
        pthread_mutex_init(&sim.garfos_mutex[i], NULL);
    }

    gettimeofday(&sim.start_time, NULL);

    // Criação das threads
    vector<pthread_t> threads(sim.N);
    vector<DadosFilosofo> dados(sim.N);

    for (int i = 0; i < sim.N; i++) {
        dados[i].id = i;
        dados[i].sim = &sim;
        pthread_create(&threads[i], NULL, rotina_filosofo, &dados[i]);
    }

    // Aguardar tempo de simulação
    sleep(sim.duracao_total);
    sim.ativo = false;

    // Join nas threads e limpeza
    for (int i = 0; i < sim.N; i++) {
        pthread_join(threads[i], NULL);
        pthread_mutex_destroy(&sim.garfos_mutex[i]);
    }
    pthread_mutex_destroy(&sim.print_mutex);

    // Resumo final
    cout << "\n======= RESUMO FINAL =======" << endl;
    for (int i = 0; i < sim.N; i++) {
        cout << "Filósofo " << i << " comeu " << sim.refeicoes[i] << " vezes." << endl;
    }

    return 0;
}