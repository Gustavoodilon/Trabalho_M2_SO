#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

// Parâmetros globais de execução
int N_CADEIRAS;
int TAXA_CHEGADA;
int TEMPO_ATENDIMENTO;
int DURACAO_SIMULACAO;

// Variáveis de estado e recursos compartilhados
int em_espera = 0;
int desistentes = 0;
int atendidos = 0;
int *fila; // Buffer circular para a fila de espera
int fila_frente = 0;
int fila_tras = 0;

int simulacao_ativa = 1;
char barbeiro_estado[50] = "DORME";

// Mecanismos de Sincronização
pthread_mutex_t mutex_estado;
sem_t sem_clientes;

struct timeval start_time;

// Função para imprimir o estado do sistema no formato exigido
void print_status(const char* evento) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 + (now.tv_usec - start_time.tv_usec) / 1000;
    
    int ms = elapsed_ms % 1000;
    int s = (elapsed_ms / 1000) % 60;
    int m = (elapsed_ms / (1000 * 60)) % 60;
    int h = (elapsed_ms / (1000 * 60 * 60));

    // Impressão discreta
    printf("[%02d:%02d:%02d.%03d] %s\n", h, m, s, ms, evento);
    printf("Barbeiro: %s\n", barbeiro_estado);
    printf("Fila: [");

    for(int i = 0; i < N_CADEIRAS; i++) {
        if(i < em_espera) printf("#");
        else printf(".");
    }
    printf("] (%d/%d) ->", em_espera, N_CADEIRAS);
    
    for(int i = 0; i < em_espera; i++) {
        int idx = (fila_frente + i) % N_CADEIRAS;
        printf(" C%d", fila[idx]);
    }
    printf("\n");
    printf("Contadores: atendidos = %d | desistentes = %d | em espera = %d\n\n", atendidos, desistentes, em_espera);
}

// Thread do Barbeiro
void* barbeiro_thread(void* arg) {
    char evento[150];
    
    while(simulacao_ativa) {
        // Barbeiro dorme se não há clientes. 
        // Se houver clientes (semáforo > 0), ele passa direto sem bloquear.
        sem_wait(&sem_clientes);
        
        // Verifica se a simulação acabou e ele foi acordado para encerrar
        if (!simulacao_ativa) break; 

        // Acordou ou pegou o próximo cliente da fila
        pthread_mutex_lock(&mutex_estado);
        int cliente_atual = fila[fila_frente];
        fila_frente = (fila_frente + 1) % N_CADEIRAS;
        em_espera--;
        sprintf(barbeiro_estado, "ATENDE C%d", cliente_atual);
        sprintf(evento, "Barbeiro iniciou atendimento do cliente C%d", cliente_atual);
        print_status(evento);
        pthread_mutex_unlock(&mutex_estado);

        // Simula o tempo de atendimento (Distribuição uniforme de 50% a 150% do tempo médio)
        int delay = (TEMPO_ATENDIMENTO / 2) + (rand() % (TEMPO_ATENDIMENTO + 1));
        usleep(delay * 1000); // Converte para microsegundos

        // Concluiu o atendimento
        pthread_mutex_lock(&mutex_estado);
        atendidos++;
        
        // CORREÇÃO DA LÓGICA AQUI:
        // Ele SÓ VAI DORMIR se não houver NINGUÉM na fila no momento em que termina o corte.
        if (em_espera == 0) {
            strcpy(barbeiro_estado, "DORME");
        }
        
        sprintf(evento, "Barbeiro concluiu atendimento do cliente C%d", cliente_atual);
        print_status(evento);
        pthread_mutex_unlock(&mutex_estado);
    }
    return NULL;
}

// Thread de um Cliente
void* cliente_thread(void* arg) {
    int id = *(int*)arg;
    free(arg); // Libera a memória alocada para o ID
    char evento[150];

    pthread_mutex_lock(&mutex_estado);
    
    if (!simulacao_ativa) {
        pthread_mutex_unlock(&mutex_estado);
        return NULL;
    }

    // Se a sala estiver cheia, o cliente vai embora
    if (em_espera >= N_CADEIRAS) {
        desistentes++;
        sprintf(evento, "Cliente C%d chegou, mas desistiu por falta de cadeira", id);
        print_status(evento);
        pthread_mutex_unlock(&mutex_estado);
    } else {
        // Se há cadeira livre, o cliente entra na fila e aguarda
        fila[fila_tras] = id;
        fila_tras = (fila_tras + 1) % N_CADEIRAS;
        em_espera++;
        sprintf(evento, "Cliente C%d chegou e entrou na fila", id);
        print_status(evento);
        
        // Sinaliza (acorda) o barbeiro que um cliente chegou
        sem_post(&sem_clientes);
        pthread_mutex_unlock(&mutex_estado);
    }
    
    // A thread do cliente finaliza seu ciclo de vida. O barbeiro gerencia a contagem de "atendido".
    return NULL;
}

int main(int argc, char* argv[]) {
    // Validação de entrada de parâmetros
    if(argc != 5) {
        printf("Uso: %s <num_cadeiras> <taxa_chegada_ms> <tempo_atend_ms> <duracao_s>\n", argv[0]);
        return 1;
    }

    N_CADEIRAS = atoi(argv[1]);
    TAXA_CHEGADA = atoi(argv[2]);
    TEMPO_ATENDIMENTO = atoi(argv[3]);
    DURACAO_SIMULACAO = atoi(argv[4]);

    // Inicialização
    fila = (int*)malloc(sizeof(int) * N_CADEIRAS);
    pthread_mutex_init(&mutex_estado, NULL);
    sem_init(&sem_clientes, 0, 0); // Semáforo inicia em 0 (Barbeiro dormindo)
    srand(time(NULL));
    gettimeofday(&start_time, NULL);

    // Cria a thread única do barbeiro
    pthread_t barbeiro;
    pthread_create(&barbeiro, NULL, barbeiro_thread, NULL);

    int client_id = 1;
    struct timeval now;
    
    // Loop principal: gerador de chegadas de clientes
    while(1) {
        gettimeofday(&now, NULL);
        long elapsed_s = now.tv_sec - start_time.tv_sec;
        
        // Verifica se o tempo total da simulação foi atingido
        if (elapsed_s >= DURACAO_SIMULACAO) {
            break; 
        }

        // Tempo aleatório até a chegada do próximo cliente (50% a 150% do médio)
        int delay = (TAXA_CHEGADA / 2) + (rand() % (TAXA_CHEGADA + 1));
        usleep(delay * 1000);

        if (simulacao_ativa) {
            // Cria uma thread para o novo cliente
            pthread_t t;
            int* id = (int*)malloc(sizeof(int));
            *id = client_id++;
            pthread_create(&t, NULL, cliente_thread, id);
            pthread_detach(t); // Desanexa a thread para que o SO libere os recursos automaticamente ao finalizar
        }
    }

    // Finalização controlada do sistema
    pthread_mutex_lock(&mutex_estado);
    simulacao_ativa = 0;
    pthread_mutex_unlock(&mutex_estado);

    // Acorda o barbeiro (caso ele esteja dormindo) para que a thread possa encerrar 
    for(int i = 0; i <= N_CADEIRAS; i++) {
        sem_post(&sem_clientes); 
    }

    pthread_join(barbeiro, NULL);

    // Limpeza de recursos
    free(fila);
    pthread_mutex_destroy(&mutex_estado);
    sem_destroy(&sem_clientes);

    printf("\n--- FIM DA SIMULACAO ---\n");
    printf("Total de Clientes Atendidos: %d\n", atendidos);
    printf("Total de Clientes Desistentes: %d\n", desistentes);

    return 0;
}