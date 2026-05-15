#include <iostream>   
#include <cstdio>       // Para o printf formatar os zeros do relógio [00:00:00.000]
#include <cstdlib>      // Para atoi(), rand(), srand()
#include <pthread.h>    // Essencial: Trava de Regiões Críticas (Mutex)
#include <semaphore.h>  // Essencial: Controle de dormir/acordar do barbeiro
#include <unistd.h>     // Essencial: usleep() simula o tempo de corte/chegada
#include <sys/time.h>   // Essencial: Timestamp dos eventos

using namespace std;

// Parâmetros globais informados no terminal
int N_CADEIRAS;
int TAXA_CHEGADA;
int TEMPO_ATENDIMENTO;
int DURACAO_SIMULACAO;

// Variáveis de Estado Compartilhadas (Região Crítica - requer Mutex)
int em_espera = 0;
int desistentes = 0;
int atendidos = 0;
int* fila; // Buffer Circular para representar as cadeiras
int fila_frente = 0; // Índice onde o barbeiro chama o próximo
int fila_tras = 0;   // Índice onde o novo cliente senta
int simulacao_ativa = 1; // Flag de encerramento

// Controle do estado do barbeiro: 0 = Dormindo, >0 = Cortando cabelo do cliente ID
int cliente_no_barbeiro = 0; 

// Mecanismos de Sincronização
pthread_mutex_t mutex_estado; // Evita Condição de Corrida nas variáveis acima
sem_t sem_clientes;           // Semáforo: Controla se o barbeiro dorme (0) ou trabalha (>0)
struct timeval start_time;

// Função apenas visual. Formata o relógio e os arrays na tela.
void print_status(const char* evento, int id_envolvido) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 + (now.tv_usec - start_time.tv_usec) / 1000;
    
    int ms = elapsed_ms % 1000;
    int s = (elapsed_ms / 1000) % 60;
    int m = (elapsed_ms / (1000 * 60)) % 60;
    int h = (elapsed_ms / (1000 * 60 * 60));

    printf("[%02d:%02d:%02d.%03d] ", h, m, s, ms);
    if (id_envolvido > 0) {
        printf("%s C%d\n", evento, id_envolvido);
    } else {
        printf("%s\n", evento); 
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

// Thread Consumidora: O Barbeiro
void* barbeiro_thread(void* arg) {
    while(simulacao_ativa) {
        // SEMÁFORO: Se não tem clientes na fila (valor 0), a thread bloqueia (Barbeiro Dorme).
        // Se um cliente der um sem_post, ele acorda e prossegue.
        sem_wait(&sem_clientes); 
        
        if (!simulacao_ativa) break; 

        // TRAVA MUTEX: Acessa a fila com segurança para puxar o próximo cliente
        pthread_mutex_lock(&mutex_estado);
        int cliente_atual = fila[fila_frente];
        fila_frente = (fila_frente + 1) % N_CADEIRAS;
        em_espera--;
        
        cliente_no_barbeiro = cliente_atual;
        print_status("Barbeiro iniciou atendimento do cliente", cliente_atual);
        // LIBERA MUTEX: Ele solta a trava ANTES de cortar o cabelo. 
        // Se não fizesse isso, novos clientes não poderiam entrar e sentar enquanto ele trabalha!
        pthread_mutex_unlock(&mutex_estado);

        // Tempo de corte de cabelo simulado
        int delay = (TEMPO_ATENDIMENTO / 2) + (rand() % (TEMPO_ATENDIMENTO + 1));
        usleep(delay * 1000); 

        // TRAVA MUTEX: Para contabilizar o atendimento e atualizar seu estado
        pthread_mutex_lock(&mutex_estado);
        atendidos++;
        
        // Só volta a dormir se a fila estiver zerada neste exato momento
        if (em_espera == 0) {
            cliente_no_barbeiro = 0; 
        }
        
        print_status("Barbeiro concluiu atendimento do cliente", cliente_atual);
        pthread_mutex_unlock(&mutex_estado);
    }
    return NULL;
}

// Thread Produtora: O Cliente
void* cliente_thread(void* arg) {
    int id = *(int*)arg;
    delete (int*)arg; // Libera a memória do ID passado via ponteiro
    
    // TRAVA MUTEX: O cliente vai checar as cadeiras. Precisa garantir que a fila não mude de tamanho.
    pthread_mutex_lock(&mutex_estado);
    
    if (!simulacao_ativa) {
        pthread_mutex_unlock(&mutex_estado);
        return NULL;
    }

    if (em_espera >= N_CADEIRAS) {
        // A barbearia está cheia. O cliente vai embora (desiste).
        desistentes++;
        print_status("Cliente chegou, mas desistiu por falta de cadeira:", id);
        pthread_mutex_unlock(&mutex_estado);
    } else {
        // Tem lugar na fila. O cliente senta (entra no buffer circular).
        fila[fila_tras] = id;
        fila_tras = (fila_tras + 1) % N_CADEIRAS;
        em_espera++;
        print_status("Cliente chegou e entrou na fila:", id);
        
        // SEMÁFORO (Acorda Barbeiro): Incrementa o semáforo avisando que tem gente na fila.
        sem_post(&sem_clientes); 
        pthread_mutex_unlock(&mutex_estado);
    }
    
    // A thread do cliente finaliza aqui. Quem gerencia o corte final dele é o barbeiro.
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

    // Alocação dinâmica da fila de cadeiras
    fila = new int[N_CADEIRAS];
    
    // Inicialização das sincronizações (Semáforo começa em 0 = barbeiro dormindo)
    pthread_mutex_init(&mutex_estado, NULL);
    sem_init(&sem_clientes, 0, 0); 
    srand(time(NULL));
    gettimeofday(&start_time, NULL);

    // Cria a única thread consumidora
    pthread_t barbeiro;
    pthread_create(&barbeiro, NULL, barbeiro_thread, NULL);

    int client_id = 1;
    struct timeval now;
    
    // Loop principal atua como gerador de clientes (Geração de Threads)
    while(1) {
        gettimeofday(&now, NULL);
        long elapsed_s = now.tv_sec - start_time.tv_sec;
        
        if (elapsed_s >= DURACAO_SIMULACAO) {
            break; // Tempo da simulação acabou
        }

        // Simula intervalo de chegada aleatório
        int delay = (TAXA_CHEGADA / 2) + (rand() % (TAXA_CHEGADA + 1));
        usleep(delay * 1000);

        if (simulacao_ativa) {
            pthread_t t;
            int* id = new int(client_id++); 
            pthread_create(&t, NULL, cliente_thread, id);
            
            // detach(): O SO limpa os recursos da thread do cliente automaticamente ao invés de usar join.
            pthread_detach(t); 
        }
    }

    // Finalização Graciosa do sistema
    pthread_mutex_lock(&mutex_estado);
    simulacao_ativa = 0;
    pthread_mutex_unlock(&mutex_estado);

    // Se o barbeiro estiver dormindo (sem_wait), ele ficará preso para sempre. 
    // Damos posts artificiais só para ele acordar, ver que a simulação acabou, e encerrar a thread dele.
    for(int i = 0; i <= N_CADEIRAS; i++) {
        sem_post(&sem_clientes); 
    }

    // Espera o barbeiro fechar a loja
    pthread_join(barbeiro, NULL);

    // Limpeza pesada
    pthread_mutex_destroy(&mutex_estado);
    sem_destroy(&sem_clientes);
    delete[] fila;

    cout << "\n--- FIM DA SIMULACAO ---\n";
    cout << "Total de Clientes Atendidos: " << atendidos << "\n";
    cout << "Total de Clientes Desistentes: " << desistentes << "\n";

    return 0;
}