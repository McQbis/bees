#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <vector>

#define REQUEST 1
#define ACK 2
#define RELEASE 3
#define ACK_FLOWER 4
#define REQUEST_FLOWER 5
#define RELEASE_FLOWER 6
#define ACK_DEAD 7

int rank, size;
int lamport_clock = 0;
int hp = 5;
int eggs = 0;

pthread_mutex_t clock_mutex, queue_mutex, check_cond_mutex;
pthread_cond_t check_cond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t queue_mutex_flower, check_cond_mutex_flower;
pthread_cond_t check_cond_flower = PTHREAD_COND_INITIALIZER;

pthread_mutex_t egg_mutex, dead_mutex, check_cond_mutex_dead;
pthread_cond_t check_cond_dead = PTHREAD_COND_INITIALIZER;

std::vector<std::vector<int>> request_queue; 
std::vector<int> request_queue_flower;
std::vector<int> egg_in_reed;
std::vector<bool> is_dead;

int ackCount, ackCountFlower, ackCountDead;
int P, T, K;
int reed;

void increment_lamport_clock() {
    pthread_mutex_lock(&clock_mutex);
    lamport_clock++;
    pthread_mutex_unlock(&clock_mutex);
}

void update_lamport_clock(int received_clock) {
    pthread_mutex_lock(&clock_mutex);
    lamport_clock = (lamport_clock > received_clock) ? lamport_clock + 1 : received_clock + 1;
    pthread_mutex_unlock(&clock_mutex);
}

void send_request_to_all_flower() {
    ackCountFlower = 0;

    pthread_mutex_lock(&clock_mutex);
    lamport_clock++;
    int message[3] = {lamport_clock, rank, reed};

    pthread_mutex_lock(&queue_mutex_flower);
    request_queue_flower[rank] = lamport_clock;
    pthread_mutex_unlock(&queue_mutex_flower);

    pthread_mutex_unlock(&clock_mutex);

    pthread_mutex_lock(&dead_mutex);
    for (int i = 0; i < P; i++) {
        if (i != rank && !is_dead[i]) {
            MPI_Send(&message, 3, MPI_INT, i, REQUEST_FLOWER, MPI_COMM_WORLD);
        }
    }
    pthread_mutex_unlock(&dead_mutex);
}

void send_release_to_all_flower() {
    increment_lamport_clock();
    int message[3] = {lamport_clock, rank, reed};
    pthread_mutex_lock(&dead_mutex);
    for (int i = 0; i < P; i++) {
        if (i != rank && !is_dead[i]) {
            MPI_Send(&message, 3, MPI_INT, i, RELEASE_FLOWER, MPI_COMM_WORLD);
        }
    }
    pthread_mutex_unlock(&dead_mutex);
}

void enter_critical_section() {
    // printf("[%d] Process %d entering to reed %d\n", lamport_clock, rank, reed);

    while( hp > 0 ){
        send_request_to_all_flower();

        bool can_enter = false;
        while (!can_enter) {
            pthread_mutex_lock(&check_cond_mutex_flower);
            if (ackCountFlower >= (size - 1)){
                pthread_mutex_lock(&queue_mutex_flower);
                // Check if our process has the smallest Lamport clock
                can_enter = true;
                int flowers = 0;
                for (int i = 0; i < P; i++) {
                    if (request_queue_flower[i] != -1 && (request_queue_flower[i] < request_queue_flower[rank] || 
                    (request_queue_flower[i] == request_queue_flower[rank] && i < rank))) {
                        flowers++;
                    }
                }
                if (flowers >= K) can_enter = false;
                pthread_mutex_unlock(&queue_mutex_flower);

                if(can_enter) {
                    pthread_mutex_unlock(&check_cond_mutex_flower);
                } else {
                    pthread_cond_wait(&check_cond_flower, &check_cond_mutex_flower);
				    pthread_mutex_unlock(&check_cond_mutex_flower);
                }
            } else {
                pthread_cond_wait(&check_cond_flower, &check_cond_mutex_flower);
				pthread_mutex_unlock(&check_cond_mutex_flower);
            }
        }

        // printf("[%d] Process %d entering to flower\n", lamport_clock, rank);

        printf("proces %d w sekcji krytycznej - zajmuje kwiatek\n", rank);
        hp--;
        sleep(1);
        eggs++;
        printf("Procees %d lay %d\n", rank, eggs);
        // printf("[%d] Process %d exiting from flower\n", lamport_clock, rank);
        printf("proces %d wychodzi z sekcji krytycznej - zwalnia kwiatek\n", rank);

        
        send_release_to_all_flower();

        pthread_mutex_lock(&queue_mutex_flower);
        request_queue_flower[rank] = -1;
        pthread_mutex_unlock(&queue_mutex_flower);
    }

    // printf("[%d] Process %d exiting from reed %d\n", lamport_clock, rank, reed);
}

void send_request_to_all() {
    ackCount = 0;

    pthread_mutex_lock(&clock_mutex);
    lamport_clock++;
    int message[3] = {lamport_clock, rank, reed};

    pthread_mutex_lock(&queue_mutex);
    request_queue[reed][rank] = lamport_clock;
    pthread_mutex_unlock(&queue_mutex);

    pthread_mutex_unlock(&clock_mutex);

    pthread_mutex_lock(&dead_mutex);
    for (int i = 0; i < P; i++) {
        if (i != rank && !is_dead[i]) {
            MPI_Send(&message, 3, MPI_INT, i, REQUEST, MPI_COMM_WORLD);
        }
    }
    pthread_mutex_unlock(&dead_mutex);
}

void send_release_to_all() {
    ackCountDead = 0;
    increment_lamport_clock();
    int message[3] = {lamport_clock, rank, reed};
    pthread_mutex_lock(&dead_mutex);
    for (int i = 0; i < P; i++) {
        if (i != rank && !is_dead[i]) {
            MPI_Send(&message, 3, MPI_INT, i, RELEASE, MPI_COMM_WORLD);
        }
    }
    pthread_mutex_unlock(&dead_mutex);
}

void send_reply_flower(int dest) {
    if (!is_dead[dest]) {
        increment_lamport_clock();
        int message[3] = {lamport_clock, rank , reed};
        MPI_Send(&message, 2, MPI_INT, dest, ACK_FLOWER, MPI_COMM_WORLD);
    }
}

void send_reply(int dest) {
    if (!is_dead[dest]) {
        increment_lamport_clock();
        int message[3] = {lamport_clock, rank , reed};
        MPI_Send(&message, 2, MPI_INT, dest, ACK, MPI_COMM_WORLD);
    }
}

void send_reply_dead(int dest) {
    if (is_dead[dest]) {
        increment_lamport_clock();
        int message[3] = {lamport_clock, rank , reed};
        MPI_Send(&message, 2, MPI_INT, dest, ACK_DEAD, MPI_COMM_WORLD);
    }
}

// Function for receiving thread
void *receiver(void *arg) {
    MPI_Status status;
    int message[3];

    while (!is_dead[rank]) {
        MPI_Recv(&message, 3, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        int received_clock = message[0];
        int sender = message[1];
        int received_reed = message[2];

        update_lamport_clock(received_clock);

        if (status.MPI_TAG == REQUEST) {
            pthread_mutex_lock(&queue_mutex);
            // Add to request queue
            request_queue[received_reed][sender] = received_clock;
            pthread_mutex_unlock(&queue_mutex);
            // Send REPLY back to the requesting process
            send_reply(sender);
        } else if (status.MPI_TAG == ACK) {
            // Reply received, allowing process to enter critical section
            pthread_mutex_lock(&check_cond_mutex);
	        ackCount++;
            if (ackCount >= size - 1) { //
                pthread_cond_signal(&check_cond);
            }
            pthread_mutex_unlock(&check_cond_mutex);
        } else if (status.MPI_TAG == RELEASE) {
            pthread_mutex_lock(&queue_mutex);
            // Remove from request queue
            request_queue[received_reed][sender] = -1;
            pthread_mutex_unlock(&queue_mutex);

            pthread_mutex_lock(&egg_mutex);
            egg_in_reed[received_reed] = egg_in_reed[received_reed] - 5;
            pthread_mutex_unlock(&egg_mutex);


            pthread_mutex_lock(&dead_mutex);
            pthread_mutex_lock(&check_cond_mutex);
            pthread_mutex_lock(&check_cond_mutex_flower);
            pthread_mutex_lock(&check_cond_mutex_dead);
            size--;
            pthread_mutex_unlock(&check_cond_mutex_dead);
            pthread_mutex_unlock(&check_cond_mutex_flower);
            pthread_mutex_unlock(&check_cond_mutex);
            is_dead[sender] = true;
            pthread_mutex_unlock(&dead_mutex);

            send_reply_dead(sender);

            pthread_mutex_lock(&check_cond_mutex);
            pthread_cond_signal(&check_cond);
            pthread_mutex_unlock(&check_cond_mutex);
        } else if (status.MPI_TAG == REQUEST_FLOWER) {
            pthread_mutex_lock(&queue_mutex_flower);
            // Add to request queue
            request_queue_flower[sender] = received_clock;
            pthread_mutex_unlock(&queue_mutex_flower);
            // Send REPLY back to the requesting process
            send_reply_flower(sender);
        } else if (status.MPI_TAG == ACK_FLOWER) {
            // Reply received, allowing process to enter critical section
            pthread_mutex_lock(&check_cond_mutex_flower);
	        ackCountFlower++;
            if (ackCountFlower >= size - 1) { //
                pthread_cond_signal(&check_cond_flower);
            }
            pthread_mutex_unlock(&check_cond_mutex_flower);
        } else if (status.MPI_TAG == RELEASE_FLOWER) {
            pthread_mutex_lock(&queue_mutex_flower);
            // Remove from request queue
            request_queue_flower[sender] = -1;
            pthread_mutex_unlock(&queue_mutex_flower);

            pthread_mutex_lock(&check_cond_mutex_flower);
            pthread_cond_signal(&check_cond_flower);
            pthread_mutex_unlock(&check_cond_mutex_flower);
        } else if (status.MPI_TAG == ACK_DEAD) {
            // Reply received, allowing process to enter critical section
            pthread_mutex_lock(&check_cond_mutex_dead);
	        ackCountDead++;
            if (ackCountDead >= size - 1) { //
                pthread_cond_signal(&check_cond_dead);
            }
            pthread_mutex_unlock(&check_cond_mutex_dead);
        } 
    }
    return 0;
}

// Function for sending thread (sending requests)
void *sender(void *arg) {

        sleep(rand() % 5 + 1); // Wait before requesting critical section

        reed = rand()%T;
        pthread_mutex_lock(&egg_mutex);
        while(egg_in_reed[reed] == 0){
            reed = rand()%T;
        }
        pthread_mutex_unlock(&egg_mutex);

        send_request_to_all();

        // Wait for REPLY messages from all processes
        // Once all processes have sent a reply and it's our turn (smallest Lamport clock),
        // enter the critical section.

        bool can_enter = false;
        while (!can_enter) {
            pthread_mutex_lock(&check_cond_mutex);
            if (ackCount >= (size - 1)){
                pthread_mutex_lock(&queue_mutex);
                // Check if our process has the smallest Lamport clock
                can_enter = true;
                for (int i = 0; i < P; i++) {
                    if (request_queue[reed][i] != -1 && (request_queue[reed][i] < request_queue[reed][rank] || 
                    (request_queue[reed][i] == request_queue[reed][rank] && i < rank))) {
                        can_enter = false;
                        break;
                    }
                }
                pthread_mutex_unlock(&queue_mutex);

                if(can_enter) {
                    pthread_mutex_unlock(&check_cond_mutex);
                } else {
                    pthread_cond_wait(&check_cond, &check_cond_mutex);
				    pthread_mutex_unlock(&check_cond_mutex);
                }
            } else {
                pthread_cond_wait(&check_cond, &check_cond_mutex);
				pthread_mutex_unlock(&check_cond_mutex);
            }
        }

        printf("proces %d w sekcji krytycznej - zajmuje trzcinę %d\n", rank, reed);
        // Enter critical section
        enter_critical_section();

        printf("proces %d wychodzi z sekcji krytycznej - zwalnia trzcinę %d\n", rank, reed);

        // Send RELEASE to all processes
        send_release_to_all();

        bool can_die = false;
        while (!can_die) {
            pthread_mutex_lock(&check_cond_mutex_dead);
            if (ackCountDead >= (size - 1)){
                can_die = true;
                pthread_mutex_unlock(&check_cond_mutex_dead);
            } else {
                pthread_cond_wait(&check_cond_dead, &check_cond_mutex_dead);
				pthread_mutex_unlock(&check_cond_mutex_dead);
            }
        }
        pthread_mutex_lock(&dead_mutex);
        is_dead[rank] = true;
        pthread_mutex_unlock(&dead_mutex);
        printf("Process %d is dead\n", rank);
    
    return 0;
}

int main(int argc, char *argv[]) {

    T = atoi(argv[1]); // Number of reeds
    K = atoi(argv[2]); // Number of flowers

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    P = size;

    pthread_mutex_init(&clock_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_mutex_init(&check_cond_mutex, NULL);

    pthread_mutex_init(&queue_mutex_flower, NULL);
    pthread_mutex_init(&check_cond_mutex_flower, NULL);

    pthread_mutex_init(&dead_mutex, NULL);
    pthread_mutex_init(&egg_mutex, NULL);
    pthread_mutex_init(&check_cond_mutex_dead, NULL);

    std::vector<std::vector<int>> matrix(T, std::vector<int>(size, -1));
    request_queue = matrix;

    request_queue_flower = std::vector<int>(size, -1);
    egg_in_reed = std::vector<int>(T, 15);
    is_dead = std::vector<bool>(size, false);

    srand(rank);
    pthread_mutex_lock(&clock_mutex);
    lamport_clock = rand() % size;
    pthread_mutex_unlock(&clock_mutex);

    pthread_t send_thread, recv_thread;

    // Create sending and receiving threads
    pthread_create(&send_thread, NULL, sender, NULL);
    pthread_create(&recv_thread, NULL, receiver, NULL);

    // Join the threads
    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);

    pthread_mutex_destroy(&clock_mutex);
    pthread_mutex_destroy(&queue_mutex);

    MPI_Finalize();

    return 0;
}
