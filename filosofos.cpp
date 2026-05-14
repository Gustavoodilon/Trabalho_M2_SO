#include <iostream>   
#include <cstdio>       // Para o printf (formatar o relógio sem <iomanip>)
#include <cstdlib>      // Para atoi(), rand(), srand()
#include <pthread.h>    // Essencial: Threads e Mutexes
#include <unistd.h>     // Essencial: usleep() e sleep()
#include <sys/time.h>   // Essencial: gettimeofday() para o log de tempo

using namespace std;

// Estados do filósofo
enum Estado { PENS, FOME, COME };

// Estrutura raiz usando apenas ponteiros nativos (sem <vector>)
struct Simulacao {
    int N;
    int duracao_total;
    int t_pensar_min, t_pensar_max;
    int t_comer_min, t_comer_max;
    struct timeval start_time;
    int ativo; // Controle sem <atomic> (1 = rodando, 0 = fim)

    pthread_mutex_t* garfos_mutex;
    bool* garfos_estado;
    Estado* filosofos_estado;
    int* refeicoes;
    pthread_mutex_t print_mutex;
};

struct DadosFilosofo {
    int id;
    Simulacao* sim;
};

// Função de impressão simplificada sem usar <string>
void imprimir_evento(Simulacao* sim, int id, const char* de, const char* para) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long millis = (now.tv_sec - sim->start_time.tv_sec) * 1000 + (now.tv_usec - sim->start_time.tv_usec) / 1000;
    
    int hh = millis / 3600000;
    int mm = (millis % 3600000) / 60000;
    int ss = (millis % 60000) / 1000;
    int mmm = millis % 1000;

    // printf para colocar os zeros alinhados [00:00:00.000]
    printf("[%02d:%02d:%02d.%03d] F%d: %s > %s\n", hh, mm, ss, mmm, id, de, para);
    
    cout << "Garfos: ";
    for(int i = 0; i < sim->N; ++i) 
        cout << "[" << (sim->garfos_estado[i] ? "X" : "O") << "]" << (i == sim->N - 1 ? "" : " ");
    cout << "\n";

    cout << "Filósofos: ";
    for(int i = 0; i < sim->N; ++i) {
        const char* st = (sim->filosofos_estado[i] == PENS) ? "PENS" : (sim->filosofos_estado[i] == FOME ? "FOME" : "COME");
        cout << "F" << i << ":" << st << (i == sim->N - 1 ? "" : " | ");
    }
    cout << "\n";

    cout << "Refeições: ";
    for(int i = 0; i < sim->N; ++i)
        cout << "F" << i << ":" << sim->refeicoes[i] << (i == sim->N - 1 ? "" : " | ");
    cout << "\n------------------------------------------------------------\n";
}

// Thread do Filósofo
void* rotina_filosofo(void* arg) {
    DadosFilosofo* dados = (DadosFilosofo*)arg;
    Simulacao* sim = dados->sim;
    int id = dados->id;

    // Definição dos garfos (esquerda e direita)
    int g_esq = id;
    int g_dir = (id + 1) % sim->N;

    // Prevenção de Deadlock: sempre pegar o menor índice primeiro
    int primeiro = (g_esq < g_dir) ? g_esq : g_dir;
    int segundo  = (g_esq < g_dir) ? g_dir : g_esq;

    while (sim->ativo) {
        // TEMPO PENSANDO (Usando rand() nativo no lugar da biblioteca <random>)
        int range_pensar = sim->t_pensar_max - sim->t_pensar_min + 1;
        int delay_pensar = sim->t_pensar_min + (rand() % range_pensar);
        usleep(delay_pensar * 1000);

        if (!sim->ativo) break; 

        // ESTADO: FOME
        pthread_mutex_lock(&sim->print_mutex);
        sim->filosofos_estado[id] = FOME;
        imprimir_evento(sim, id, "PENS", "FOME");
        pthread_mutex_unlock(&sim->print_mutex);

        // PEGAR GARFOS FÍSICOS
        pthread_mutex_lock(&sim->garfos_mutex[primeiro]);
        pthread_mutex_lock(&sim->garfos_mutex[segundo]);

        // ESTADO: COMENDO
        pthread_mutex_lock(&sim->print_mutex);
        sim->garfos_estado[primeiro] = true;
        sim->garfos_estado[segundo] = true;
        sim->filosofos_estado[id] = COME;
        sim->refeicoes[id]++;
        imprimir_evento(sim, id, "FOME", "COME");
        pthread_mutex_unlock(&sim->print_mutex);
        
        // TEMPO COMENDO
        int range_comer = sim->t_comer_max - sim->t_comer_min + 1;
        int delay_comer = sim->t_comer_min + (rand() % range_comer);
        usleep(delay_comer * 1000);

        // ESTADO: PENSANDO (Largando os garfos)
        pthread_mutex_lock(&sim->print_mutex);
        sim->garfos_estado[segundo] = false;
        sim->garfos_estado[primeiro] = false;
        sim->filosofos_estado[id] = PENS;
        imprimir_evento(sim, id, "COME", "PENS");
        pthread_mutex_unlock(&sim->print_mutex);

        // DEVOLVE OS GARFOS
        pthread_mutex_unlock(&sim->garfos_mutex[segundo]);
        pthread_mutex_unlock(&sim->garfos_mutex[primeiro]);
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        cout << "Uso: " << argv[0] << " N DURACAO PENS_MIN PENS_MAX COMER_MIN COMER_MAX\n";
        return 1;
    }

    Simulacao sim;
    sim.N = atoi(argv[1]);
    
    if (sim.N < 3) {
        cout << "Erro: O número de filósofos (N) deve ser pelo menos 3.\n";
        return 1;
    }

    sim.duracao_total = atoi(argv[2]);
    sim.t_pensar_min = atoi(argv[3]);
    sim.t_pensar_max = atoi(argv[4]);
    sim.t_comer_min = atoi(argv[5]);
    sim.t_comer_max = atoi(argv[6]);
    sim.ativo = 1;

    // Alocação manual usando 'new[]' ao invés de <vector>
    sim.garfos_mutex = new pthread_mutex_t[sim.N];
    sim.garfos_estado = new bool[sim.N];
    sim.filosofos_estado = new Estado[sim.N];
    sim.refeicoes = new int[sim.N];

    pthread_mutex_init(&sim.print_mutex, NULL);

    for (int i = 0; i < sim.N; i++) {
        sim.garfos_estado[i] = false;
        sim.filosofos_estado[i] = PENS;
        sim.refeicoes[i] = 0;
        pthread_mutex_init(&sim.garfos_mutex[i], NULL);
    }

    srand(time(NULL)); // Inicializa a semente do gerador de aleatórios
    gettimeofday(&sim.start_time, NULL);

    // Threads e Dados com alocação manual
    pthread_t* threads = new pthread_t[sim.N];
    DadosFilosofo* dados = new DadosFilosofo[sim.N];

    for (int i = 0; i < sim.N; i++) {
        dados[i].id = i;
        dados[i].sim = &sim;
        pthread_create(&threads[i], NULL, rotina_filosofo, &dados[i]);
    }

    // Timer principal
    sleep(sim.duracao_total);
    sim.ativo = 0;

    // Finalização e Limpeza
    for (int i = 0; i < sim.N; i++) {
        pthread_join(threads[i], NULL);
        pthread_mutex_destroy(&sim.garfos_mutex[i]);
    }
    pthread_mutex_destroy(&sim.print_mutex);

    // Resumo
    cout << "\n======= RESUMO FINAL =======\n";
    for (int i = 0; i < sim.N; i++) {
        cout << "Filósofo " << i << " comeu " << sim.refeicoes[i] << " vezes.\n";
    }

    // Liberação de toda a memória alocada dinamicamente com 'delete[]'
    delete[] sim.garfos_mutex;
    delete[] sim.garfos_estado;
    delete[] sim.filosofos_estado;
    delete[] sim.refeicoes;
    delete[] threads;
    delete[] dados;

    return 0;
}