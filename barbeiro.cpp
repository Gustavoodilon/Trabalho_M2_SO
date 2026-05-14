#include <iostream>   
#include <cstdio>       // Para o printf (essencial para formatar o relógio [00:00:00] sem bibliotecas extras)
#include <cstdlib>      // Para atoi(), rand(), srand()
#include <pthread.h>    // Essencial: Threads e Mutexes
#include <semaphore.h>  // Essencial: Semáforos
#include <unistd.h>     // Essencial: usleep() para simular o tempo de corte/espera
#include <sys/time.h>   // Essencial: gettimeofday() para o log de tempo

using namespace std;

// Parâmetros globais
int N_CADEIRAS;
int TAXA_CHEGADA;
int TEMPO_ATENDIMENTO;
int DURACAO_SIMULACAO;

// Variáveis de estado
int em_espera = 0;
int desistentes = 0;
int atendidos = 0;
int* fila; // Fila volta a ser um ponteiro simples (sem usar <vector>)
int fila_frente = 0;
int fila_tras = 0;
int simulacao_ativa = 1;

// Controle de estado sem usar <string> (0 = Dormindo, >0 = ID do cliente sendo atendido)
int cliente_no_barbeiro = 0; 

// Mecanismos de Sincronização
pthread_mutex_t mutex_estado;
sem_t sem_clientes;
struct timeval start_time;

// Função para imprimir os eventos e o estado
void print_status(const char* evento, int id_envolvido) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 + (now.tv_usec - start_time.tv_usec) / 1000;
    
    int ms = elapsed_ms % 1000;
    int s = (elapsed_ms / 1000) % 60;
    int m = (elapsed_ms / (1000 * 60)) % 60;
    int h = (elapsed_ms / (1000 * 60 * 60));

    // Usando printf para formatar os zeros sem precisar da biblioteca <iomanip>
    printf("[%02d:%02d:%02d.%03d] ", h, m, s, ms);
    if (id_envolvido > 0) {
        printf("%s C%d\n", evento, id_envolvido);
    } else {
        printf("%s\n", evento); // Eventos que não tem um cliente específico (ex: simulação acabou)
    }

    if (cliente_no_barbeiro == 0) cout << "Barbeiro: DORME\n";
    else cout << "Barbeiro: ATENDE C" << cliente_no_barbeiro << "\n";
    
    cout << "Fila: [";
    for(int i = 0; i < N_CADEIRAS; i++) {
        if(i < em_espera) cout << "#";
        else cout << ".";
    }
    cout << "] (" << em_espera << "/" << N_CADEIRAS << ") ->";
    
    for(int i = 0; i < em_espera; i++) {
        int idx = (fila_frente + i) % N_CADEIRAS;
        cout << " C" << fila[idx];
    }
    cout << "\nContadores: atendidos = " << atendidos 
         << " | desistentes = " << desistentes 
         << " | em espera = " << em_espera << "\n\n";
}

// Thread do Barbeiro
void* barbeiro_thread(void* arg) {
    while(simulacao_ativa) {
        sem_wait(&sem_clientes); // Dorme se semáforo for 0
        
        if (!simulacao_ativa) break; 

        pthread_mutex_lock(&mutex_estado);
        int cliente_atual = fila[fila_frente];
        fila_frente = (fila_frente + 1) % N_CADEIRAS;
        em_espera--;
        
        cliente_no_barbeiro = cliente_atual; // Atualiza status
        print_status("Barbeiro iniciou atendimento do cliente", cliente_atual);
        pthread_mutex_unlock(&mutex_estado);

        // Tempo de corte
        int delay = (TEMPO_ATENDIMENTO / 2) + (rand() % (TEMPO_ATENDIMENTO + 1));
        usleep(delay * 1000); 

        pthread_mutex_lock(&mutex_estado);
        atendidos++;
        
        if (em_espera == 0) {
            cliente_no_barbeiro = 0; // Volta a dormir
        }
        
        print_status("Barbeiro concluiu atendimento do cliente", cliente_atual);
        pthread_mutex_unlock(&mutex_estado);
    }
    return NULL;
}

// Thread de um Cliente
void* cliente_thread(void* arg) {
    int id = *(int*)arg;
    delete (int*)arg; 
    
    pthread_mutex_lock(&mutex_estado);
    
    if (!simulacao_ativa) {
        pthread_mutex_unlock(&mutex_estado);
        return NULL;
    }

    if (em_espera >= N_CADEIRAS) {
        desistentes++;
        print_status("Cliente chegou, mas desistiu por falta de cadeira:", id);
        pthread_mutex_unlock(&mutex_estado);
    } else {
        fila[fila_tras] = id;
        fila_tras = (fila_tras + 1) % N_CADEIRAS;
        em_espera++;
        print_status("Cliente chegou e entrou na fila:", id);
        
        sem_post(&sem_clientes); // Acorda o barbeiro
        pthread_mutex_unlock(&mutex_estado);
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    if(argc != 5) {
        cout << "Uso: " << argv[0] << " <num_cadeiras> <taxa_chegada_ms> <tempo_atend_ms> <duracao_s>\n";
        return 1;
    }

    N_CADEIRAS = atoi(argv[1]);
    TAXA_CHEGADA = atoi(argv[2]);
    TEMPO_ATENDIMENTO = atoi(argv[3]);
    DURACAO_SIMULACAO = atoi(argv[4]);

    // Alocação dinâmica raiz em C++
    fila = new int[N_CADEIRAS];
    
    pthread_mutex_init(&mutex_estado, NULL);
    sem_init(&sem_clientes, 0, 0); 
    srand(time(NULL));
    gettimeofday(&start_time, NULL);

    pthread_t barbeiro;
    pthread_create(&barbeiro, NULL, barbeiro_thread, NULL);

    int client_id = 1;
    struct timeval now;
    
    while(1) {
        gettimeofday(&now, NULL);
        long elapsed_s = now.tv_sec - start_time.tv_sec;
        
        if (elapsed_s >= DURACAO_SIMULACAO) {
            break; 
        }

        int delay = (TAXA_CHEGADA / 2) + (rand() % (TAXA_CHEGADA + 1));
        usleep(delay * 1000);

        if (simulacao_ativa) {
            pthread_t t;
            int* id = new int(client_id++); 
            pthread_create(&t, NULL, cliente_thread, id);
            pthread_detach(t); 
        }
    }

    // Finalização
    pthread_mutex_lock(&mutex_estado);
    simulacao_ativa = 0;
    pthread_mutex_unlock(&mutex_estado);

    for(int i = 0; i <= N_CADEIRAS; i++) {
        sem_post(&sem_clientes); 
    }

    pthread_join(barbeiro, NULL);

    pthread_mutex_destroy(&mutex_estado);
    sem_destroy(&sem_clientes);
    
    // Libera a memória alocada no início
    delete[] fila;

    cout << "\n--- FIM DA SIMULACAO ---\n";
    cout << "Total de Clientes Atendidos: " << atendidos << "\n";
    cout << "Total de Clientes Desistentes: " << desistentes << "\n";

    return 0;
}