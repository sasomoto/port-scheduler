/*
 * scheduler.c - Port Management Scheduler Program
 * CS-F372 Operating Systems, Assignment 2
 *
 * Fully self-contained implementation with:
 * - Complete ship queue management
 * - Emergency ship handling
 * - Accurate dock availability tracking
 * - Smart dock + crane scheduling (bipartite matching)
 * - Modular internal logic
 * - IPC compliant with validation.out
 * - Auth string guessing stubbed
 */




#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>


//defining constants
 #define MAX_DOCKS 30
 #define MAX_AUTH_STRING_LEN 100
 #define MAX_NEW_REQUESTS 100
 #define MAX_CARGO_COUNT 200
 #define MAX_SOLVERS 8
 #define MAX_SHIPS 12000
 #define MAX_CRANES 30
 #define MAX_AUTH_STRING_LEN 100
#define SOLVER_MTYPE_SET_DOCK 1
#define SOLVER_MTYPE_GUESS 2
#define SOLVER_MTYPE_RESPONSE 3


// Mapping of digits to characters
char map[] = ".56789";


volatile sig_atomic_t found = 0;  // Global flag to indicate correct guess found
int solverQueues[8];



 /* -------------------- Struct Definitions -------------------- */
 typedef struct {
     long mtype;
     int timestep;
     int shipId;
     int direction;
     int dockId;
     int cargoId;
     int isFinished;
     union {
         int numShipRequests;
         int craneId;
     } data;
 } MessageStruct;
 
 typedef struct {
     int shipId;
     int timestep;
     int category;
     int direction;
     int emergency;
     int waitingTime;
     int numCargo;
     int cargo[MAX_CARGO_COUNT];
 } ShipRequest;
 
 typedef struct {
     char authStrings[MAX_DOCKS][MAX_AUTH_STRING_LEN];
     ShipRequest newShipRequests[MAX_NEW_REQUESTS];
 } MainSharedMemory;
 
 typedef struct {
    int capacity;
    int id;
} Crane;
typedef struct {
    int weight;
    int id;
} Cargo;

typedef struct {
    int dockId;
    int category;
    int numCranes;
    Crane cranes[MAX_CRANES];
    int dockFreeAt;
    int lastShipId;
    int lastDirection;
    int dockedAt;
    int lastCargoMove;
    int pendingUndock;
} Dock;
 
 typedef struct {
     int active;
     int shipId;
     int direction;
     int category;
     int emergency;
     int arrival;
     int waitingTime;
     int numCargo;
     Cargo cargo[MAX_CARGO_COUNT];
     int docked;
     int dockId;
     int dockTime;
     int cargoMoved;
     int undockTime;
 } Ship;
 // Node to queue all the messages
 typedef struct ScheduledMessage {
     int timestep;
     MessageStruct message;
     struct ScheduledMessage* next;
 } ScheduledMessage;
 // Struct to pass arguments to threads
typedef struct {
    long long start;
    long long end;
    int length;
    int thread_id;
    int msgqid;
    int dockId;
    char* correct_guess;
} thread_data_t;


typedef struct SolverRequest {
    long mtype;
    int dockId;
    char authStringGuess[MAX_AUTH_STRING_LEN];
} SolverRequest;


typedef struct SolverResponse {
    long mtype;
    int guessIsCorrect;
} SolverResponse;

// Compare functions for sorting
 int compare_desc_crane(const void* a, const void* b) {
    Crane* ca = (Crane*)a;
    Crane* cb = (Crane*)b;
    return cb->capacity - ca->capacity;
}
int compare_desc_docks(const void* a, const void* b) {
    Dock* da = (Dock*)a;
    Dock* db = (Dock*)b;
    return db->category - da->category;
}
int compare_desc_int(const void* a, const void* b) {
    return (*(int*)b) - (*(int*)a);
}

int compare_desc_cargo(const void* a, const void* b) {
    int aw = ((const Cargo*)a)->weight;
    int bw = ((const Cargo*)b)->weight;
    return bw - aw;
}

void sort_cargo(Cargo* cargo, int count) {
    qsort(cargo, count, sizeof(cargo[0]), compare_desc_cargo);
}


 /* -------------------- Global Variables -------------------- */
 Dock docks[MAX_DOCKS];
 int numDocks;
 Ship allShips[MAX_SHIPS];
 int numShips = 0;
 int numSolvers = 0;
 int processingTime[MAX_SHIPS][MAX_DOCKS];
 
 int shmId, mainMsqId;
 MainSharedMemory* sharedMem;
 
 ScheduledMessage* messageQueue = NULL;
 int currentTimestep = 1;
 
 /* -------------------- Function Prototypes -------------------- */
 void read_input(const char* filename);
 void setup_ipc(int shmKey, int msqKey);
 void flush_messages();
 void enqueue_message(MessageStruct msg, int timestep);
 void insert_message_sorted(ScheduledMessage* node);
 void scheduler_step(int numNewRequests);
 void add_ship(ShipRequest* req);
 void update_dock_status();
 int simulate_processing_time(Crane* cranes, int numCranes, Cargo* cargo, int numCargo);
 void assign_ships_with_matching(int emergencyOnly);
 void initialize_docks();
 void initialize_ship(Ship* s);
 void assign_ships_greedy();
 int process_ship(Ship* s, Dock* dock, int simulate);
 char* start_guessing(int length, int num_threads, int dockId);

  /* -------------------- Debug Function -------------------- */

 void print_ship_debug(const Ship* s) {
    return;
    printf("---- Ship Debug Info ----\n");
    printf("Active        : %d\n", s->active);
    printf("Ship ID       : %d\n", s->shipId);
    printf("Direction     : %d\n", s->direction);
    printf("Category      : %d\n", s->category);
    printf("Emergency     : %d\n", s->emergency);
    printf("Arrival       : %d\n", s->arrival);
    printf("Waiting Time  : %d\n", s->waitingTime);
    printf("Num Cargo     : %d\n", s->numCargo);
    printf("Docked        : %d\n", s->docked);
    printf("Dock ID       : %d\n", s->dockId);
    printf("Dock Time     : %d\n", s->dockTime);
    printf("Cargo Moved   : %d\n", s->cargoMoved);
    printf("Undock Time   : %d\n", s->undockTime);
    printf("Cargo List    : [");
    for (int i = 0; i < s->numCargo; ++i) {
        printf("(%d:%d)", s->cargo[i].id, s->cargo[i].weight);
        if (i != s->numCargo - 1) printf(", ");
    }
    printf("]\n");
    printf("--------------------------\n");
}

/* Math functions*/

double binPow(long long x, int n) {
    double result = 1.0; // Initialize the result
    while (n > 0) {
        // If the current bit of n is 1, multiply result with x
        if (n % 2 == 1) {
            result *= x;
        }
        // Square x
        x *= x;
        // Right-shift n (divide by 2)
        n /= 2;
    }
    return result;
}



 /* -------------------- Main Function -------------------- */
 int main(int argc, char* argv[]) {
     if (argc != 2) {
         fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
         exit(EXIT_FAILURE);
     }
 
     char inputPath[100];
     snprintf(inputPath, sizeof(inputPath), "testcase%s/input.txt", argv[1]);
     read_input(inputPath);
 
     while (1) {
        printf("Checking for message !!\n");
         MessageStruct recvMsg;
         if (msgrcv(mainMsqId, &recvMsg, sizeof(MessageStruct) - sizeof(long), 1, 0) == -1) {
             perror("msgrcv failed");
             exit(EXIT_FAILURE);
         }
         

         if (recvMsg.isFinished == 1) break;
         
         currentTimestep = recvMsg.timestep;
         printf("Recived for message !!\n");
         printf("%d\n", currentTimestep);
         update_dock_status();
         scheduler_step(recvMsg.data.numShipRequests);
         flush_messages();
 
         MessageStruct stepMsg = {.mtype = 5};
         if (msgsnd(mainMsqId, &stepMsg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
             perror("msgsnd timestep");
             exit(EXIT_FAILURE);
         }
     }
     return 0;
 }
 
 /* -------------------- Input and IPC Setup -------------------- */
 void read_input(const char* filename) {
     FILE* f = fopen(filename, "r");
     if (!f) { perror("fopen failed"); exit(EXIT_FAILURE); }
     int shmKey, msqKey;
     fscanf(f, "%d %d %d", &shmKey, &msqKey, &numSolvers);
     for (int i = 0; i < numSolvers; i++) { fscanf(f, "%d", &solverQueues[i]); }
     fscanf(f, "%d", &numDocks);
     for (int i = 0; i < numDocks; i++) {
         Dock* d = &docks[i];
         fscanf(f, "%d", &d->category);
         d->dockId = i;
         d->numCranes = d->category;
         d->dockFreeAt = 1;
         d->pendingUndock = 0;
         for (int j = 0; j < d->numCranes; j++){ 
            Crane cr;
            cr.id = j;
            fscanf(f, "%d", &cr.capacity);
            d->cranes[j] = cr;
        }
     }
     fclose(f);
     initialize_docks();
     setup_ipc(shmKey, msqKey);

     for(int j = 0; j < numDocks; j++){
        printf("Dock %d : Category : %d\n",docks[j].dockId,docks[j].category);
     }
 }
 
 void setup_ipc(int shmKey, int msqKey) {
    printf("%d %d\n",shmKey,msqKey);
     shmId = shmget(shmKey, sizeof(MainSharedMemory),IPC_CREAT | 0666);
     printf("$%d\n",shmId);
     if (shmId < 0) { perror("shmget failed"); exit(EXIT_FAILURE); }

     for (int i = 0; i < numSolvers;i++){
        solverQueues[i] = msgget(solverQueues[i], 0666);
        if (solverQueues[i] < 0) { perror("msgget solver failed"); exit(EXIT_FAILURE); }
     }
     sharedMem = (MainSharedMemory*)shmat(shmId, NULL, 0);
     if (sharedMem == (void*)-1) { perror("shmat failed"); exit(EXIT_FAILURE); }
     mainMsqId = msgget(msqKey, 0666);
     if (mainMsqId < 0) { perror("msgget failed"); exit(EXIT_FAILURE); }
 }
 
 void scheduler_step(int numNewRequests) {
        printf("Schdeuler called for %d new ships!\n",numNewRequests);
    //  update_dock_status();

     for (int i = 0; i < numNewRequests; i++) add_ship(&sharedMem->newShipRequests[i]);
     for (int i = 0; i < numShips; i++) {
         Ship* s = &allShips[i];
         if (s->active && !s->docked && !s->emergency && s->direction == 1 && currentTimestep > s->arrival + s->waitingTime) {
             s->active = 0;
         }
     }
 
    //  for (int i = 0; i < numShips; i++) {
    //      Ship* s = &allShips[i];
    //      if (!s->active || s->docked) continue;
    //      for (int d = 0; d < numDocks; d++) {
    //          if (docks[d].category >= s->category && docks[d].dockFreeAt <= currentTimestep) {
    //              processingTime[i][d] = process_ship(s, &docks[d],1);
    //          } else {
    //              processingTime[i][d] = INT_MAX;
    //          }
    //      }
    //  }

    assign_ships_greedy();

     return;
     assign_ships_with_matching(1);
     assign_ships_with_matching(0);
 }
 
 void add_ship(ShipRequest* req) {
     Ship* s = &allShips[numShips++];
     s->active = 1;
     s->shipId = req->shipId;
     s->direction = req->direction;
     s->category = req->category;
     s->emergency = req->emergency;
     s->arrival = req->timestep;
     s->waitingTime = req->waitingTime;
     s->numCargo = req->numCargo;
     s->docked = 0;
     for (int i = 0; i < req->numCargo; i++){
        Cargo c = {.id = i, .weight=req->cargo[i]};
        s->cargo[i] = c;
     }

     initialize_ship(s);

     printf("Ship arrived --> Ship ID : %d, Direction : %d\n",s->shipId,s->direction);
 }
 void dock_ship(int dcs,int i){
            int time = process_ship(&allShips[i],&docks[dcs],0);
                
                docks[dcs].dockFreeAt = currentTimestep + time + 2;
                printf("Assigning ship %d to dock %d and time will be %d\n",allShips[i].shipId, dcs,time);
                allShips[i].docked = 1;
                MessageStruct dockMsg = {.mtype = 2, .dockId = docks[dcs].dockId, .shipId = allShips[i].shipId, .direction = allShips[i].direction};
                enqueue_message(dockMsg, currentTimestep);
                docks[dcs].pendingUndock = currentTimestep + time + 1;
                allShips[i].dockId = docks[dcs].dockId;
                allShips[i].dockTime = currentTimestep;
                allShips[i].undockTime = currentTimestep + time + 1;
                printf("Assigning ship %d %d to dock %d\n",allShips[i].shipId,allShips[i].direction, dcs);
 }
 void assign_ships_greedy(){
    // Assign Emergency ships first 
    
    for (size_t i = 0; i < numShips; i++)
    {
        if (!allShips[i].active || allShips[i].docked || !allShips[i].emergency) continue;
        for(int dcs = numDocks -1 ; dcs >= 0; dcs--){
            if (docks[dcs].dockFreeAt <= currentTimestep && docks[dcs].category >= allShips[i].category){
                dock_ship(dcs,i);
                break;
            }
        }
    }
    // Assign all other ships
    for (size_t i = 0; i < numShips; i++)
    {
        if (!allShips[i].active || allShips[i].docked) continue;
        for(int dcs = numDocks -1 ; dcs >= 0; dcs--){
            if (docks[dcs].dockFreeAt <= currentTimestep && docks[dcs].category >= allShips[i].category){
                
                
                // int time = process_ship(&allShips[i],&docks[dcs],0);
                // printf("Assigning ship %d to dock %d\n",allShips[i].shipId, dcs);
                // docks[dcs].dockFreeAt = currentTimestep + time + 1;
                // allShips[i].docked = 1;
                // MessageStruct dockMsg = {.mtype = 2, .dockId = docks[dcs].dockId, .shipId = allShips[i].shipId, .direction = allShips[i].direction};
                // enqueue_message(dockMsg, currentTimestep);
                // docks[dcs].pendingUndock = currentTimestep + time + 1;
                // allShips[i].dockTime = currentTimestep;
                // allShips[i].undockTime = currentTimestep + time + 2;
                // printf("Assigning ship %d %d to dock %d\n",allShips[i].shipId,allShips[i].direction, dcs);
                dock_ship(dcs,i);
                break;
            }
        }
    }
    
 }
 
void assign_ships_with_matching(int emergencyOnly) {
    int assignedDock[MAX_DOCKS] = {0};
    int assignedShip[MAX_SHIPS] = {0};

    if (emergencyOnly) {
        // Greedy emergency ship assignment: high category ships → smallest fitting docks
        typedef struct { int shipIdx; int category; } EShip;
        EShip ems[MAX_SHIPS]; int ec = 0;
        for (int i = 0; i < numShips; i++) {
            if (!allShips[i].active || allShips[i].docked || !allShips[i].emergency) continue;
            ems[ec++] = (EShip){i, allShips[i].category};
        }
        for (int i = 0; i < ec - 1; i++) {
            for (int j = i + 1; j < ec; j++) {
                if (ems[i].category < ems[j].category) {
                    EShip tmp = ems[i]; ems[i] = ems[j]; ems[j] = tmp;
                }
            }
        }
        for (int i = 0; i < ec; i++) {
            int shipIdx = ems[i].shipIdx;
            int bestDock = -1, bestCat = INT_MAX;
            for (int d = 0; d < numDocks; d++) {
                if (assignedDock[d]) continue;
                if (docks[d].dockFreeAt <= currentTimestep && docks[d].category >= allShips[shipIdx].category) {
                    if (docks[d].category < bestCat) {
                        bestCat = docks[d].category;
                        bestDock = d;
                    }
                }
            }
            if (bestDock != -1) {
                assignedDock[bestDock] = 1;
                assignedShip[shipIdx] = 1;

                Ship* s = &allShips[shipIdx];
                Dock* d = &docks[bestDock];
                int procTime = processingTime[shipIdx][bestDock];

                s->docked = 1; s->dockId = bestDock; s->dockTime = currentTimestep;
                d->dockFreeAt = currentTimestep + 1 + procTime + 1;
                d->lastShipId = s->shipId;
                d->lastDirection = s->direction;
                d->lastCargoMove = currentTimestep + 1 + procTime;
                d->pendingUndock = 1;

                MessageStruct dockMsg = {.mtype = 2, .dockId = bestDock, .shipId = s->shipId, .direction = s->direction};
                enqueue_message(dockMsg, currentTimestep);

                process_ship(s,d,0);
            }
        }
    } else {
        // Existing non-emergency assignment logic (greedy cost sort)
        typedef struct { int shipIdx, dockIdx, cost; } Pair;
        Pair candidates[MAX_SHIPS * MAX_DOCKS];
        int count = 0;

        for (int i = 0; i < numShips; i++) {
            Ship* s = &allShips[i];
            if (!s->active || s->docked || s->emergency != emergencyOnly) continue;
            for (int d = 0; d < numDocks; d++) {
                if (processingTime[i][d] != INT_MAX && !assignedDock[d]) {
                    candidates[count++] = (Pair){i, d, processingTime[i][d]};
                }
            }
        }
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (candidates[j].cost < candidates[i].cost) {
                    Pair tmp = candidates[i]; candidates[i] = candidates[j]; candidates[j] = tmp;
                }
            }
        }
        for (int i = 0; i < count; i++) {
            int sIdx = candidates[i].shipIdx, dIdx = candidates[i].dockIdx;
            if (assignedShip[sIdx] || assignedDock[dIdx]) continue;
            assignedShip[sIdx] = assignedDock[dIdx] = 1;

            Ship* s = &allShips[sIdx];
            Dock* d = &docks[dIdx];
            int procTime = candidates[i].cost;

            s->docked = 1; s->dockId = dIdx; s->dockTime = currentTimestep;
            d->dockFreeAt = currentTimestep + 1 + procTime + 1;
            d->lastShipId = s->shipId;
            d->lastDirection = s->direction;
            d->lastCargoMove = currentTimestep + 1 + procTime;
            d->pendingUndock = 1;

            MessageStruct dockMsg = {.mtype = 2, .dockId = dIdx, .shipId = s->shipId, .direction = s->direction};
            enqueue_message(dockMsg, currentTimestep);

            process_ship(s,d,0);
        }
    }
}
 
 void update_dock_status() {
    for(int i = 0; i < numShips;i++){
        if(allShips[i].docked && allShips[i].undockTime == currentTimestep) {
            printf("Undocking started !!!\n");
            
            Ship s = allShips[i];
            print_ship_debug(&s);
            char* pass = start_guessing(s.undockTime - s.dockTime -1 ,numSolvers,s.dockId);
            MessageStruct msg = {
                .mtype = 3,
                .dockId = s.dockId,
                .direction = s.direction,
                .shipId = s.shipId
            };
            strncpy(sharedMem->authStrings[s.dockId],pass,MAX_AUTH_STRING_LEN);
            printf("Sending unlock message with pass %s\n",sharedMem->authStrings[s.dockId]);
            
            if (msgsnd(mainMsqId, &msg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
                perror("msgsnd failed");
                exit(EXIT_FAILURE);
            }
        }
    }
 }
 
 /* -------------------- Message Queue -------------------- */
 void enqueue_message(MessageStruct msg, int timestep) {
     ScheduledMessage* node = malloc(sizeof(ScheduledMessage));
     node->timestep = timestep;
     node->message = msg;
     node->next = NULL;
     insert_message_sorted(node);
 }
 
 void insert_message_sorted(ScheduledMessage* node) {
     if (!messageQueue || node->timestep < messageQueue->timestep) {
         node->next = messageQueue;
         messageQueue = node;
         return;
     }
     ScheduledMessage* curr = messageQueue;
     while (curr->next && curr->next->timestep <= node->timestep) {
         curr = curr->next;
     }
     node->next = curr->next;
     curr->next = node;
 }
 
 void flush_messages() {
     while (messageQueue && messageQueue->timestep == currentTimestep) {
        MessageStruct msg = messageQueue->message;
        // printf(
        //     "MessageStruct {\n"
        //     "  mtype: %ld\n"
        //     "  timestep: %d\n"
        //     "  shipId: %d\n"
        //     "  direction: %d\n"
        //     "  dockId: %d\n"
        //     "  cargoId: %d\n"
        //     "  isFinished: %d\n"
        //     "  data: {\n"
        //     "    numShipRequests / craneId: %d\n"
        //     "  }\n"
        //     "}\n",
        //     msg.mtype,
        //     msg.timestep,
        //     msg.shipId,
        //     msg.direction,
        //     msg.dockId,
        //     msg.cargoId,
        //     msg.isFinished,
        //     msg.data.numShipRequests // or msg.data.craneId, depending on context
        // );
        
         if (msgsnd(mainMsqId, &messageQueue->message, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
             perror("msgsnd failed");
             exit(EXIT_FAILURE);
         }
         ScheduledMessage* tmp = messageQueue;
         messageQueue = messageQueue->next;
         free(tmp);
     }
 }
 
 void sort_docks() {
    qsort(&docks,numDocks,sizeof(docks[0]),compare_desc_docks);
 }


 void initialize_docks() {
    for (int i = 0; i < numDocks; i++) {
        qsort(&docks[i].cranes, docks[i].numCranes, sizeof(docks[i].cranes[0]),compare_desc_crane);
        #if DEBUG
        printf("[DEBUG] Dock %d sorted cranes:\n", docks[i].dockId);
        for (int j = 0; j < docks[i].numCranes; j++) {
            printf("  Crane ID: %d, Capacity: %d\n", docks[i].cranes[j].id, docks[i].cranes[j].capacity);
        }
        #endif
    }
    sort_docks();
    
}

void initialize_ship(Ship* s) {
    sort_cargo(s->cargo, s->numCargo);
    #if DEBUG
    printf("[DEBUG] Ship %d sorted cargo:\n", s->shipId);
    for (int j = 0; j < s->numCargo; j++) {
        printf("  Cargo[%d]: %d\n", j, s->cargo[j]);
    }
    #endif
}


int process_ship(Ship* s, Dock* dock, int simulate){
    int moved = 0, moved_at = currentTimestep + 1, currentCrane = 0;
    int cargos[MAX_CARGO_COUNT] = {0};

    while (moved < s->numCargo)
    {
        for (size_t i = 0; i < s->numCargo && currentCrane < dock->numCranes; i++)
        {
            if (cargos[s->cargo[i].id] == -1) continue;
            if(dock->cranes[currentCrane].capacity >= s->cargo[i].weight){
                moved++;
                if (!simulate){
                    MessageStruct m = {.mtype = 4, .dockId = dock->dockId, .shipId = s->shipId, .direction=s->direction, .cargoId=s->cargo[i].id, .data=dock->cranes[currentCrane].id };
                    enqueue_message(m,moved_at);
                }
                cargos[s->cargo[i].id] = -1;
                currentCrane++;
            }
        }
        currentCrane = 0;
        moved_at++;
    }
    return moved_at-currentTimestep-1;
}



void send_message(int msgqid, int dockId, const char* guess) {
    SolverRequest req;
    req.mtype = SOLVER_MTYPE_GUESS;
    req.dockId = dockId;
    strncpy(req.authStringGuess, guess, MAX_AUTH_STRING_LEN);
    msgsnd(msgqid, &req, sizeof(SolverRequest) - sizeof(long), 0);
}


// Receive the response
int receive_message(int msgqid) {
    SolverResponse res;
    msgrcv(msgqid, &res, sizeof(SolverResponse) - sizeof(long), SOLVER_MTYPE_RESPONSE, 0);
    return res.guessIsCorrect == 1;
}

void base6_to_guess(int number, int length, char* guess) {
    for (int i = length - 1; i >= 0; i--) {
        guess[i] = map[number % 6];
        number /= 6;
    }
    guess[length] = '\0';
}


// Check if the guess string is valid (no dot at start or end)
int is_valid_guess(const char* guess, int length) {
    return (guess[0] != '.' && guess[length - 1] != '.');
}


// Thread function to generate guesses and send them
void* guess_generator(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char guess[MAX_AUTH_STRING_LEN];
    int count = 0;
    for (long long num = data->start; num <= data->end && !found; num++) {
        base6_to_guess(num, data->length, guess);


        if (is_valid_guess(guess, data->length)) {
            send_message(data->msgqid, data->dockId, guess);
            count++;
            int result = receive_message(data->msgqid);
            if (result == 1) {
                found = 1;
                strncpy(data->correct_guess, guess, MAX_AUTH_STRING_LEN);
                printf("[Thread %d] Correct guess found: %s\n", data->thread_id, guess);
                break;
            }
        }
    }
    printf("[Thread %d] Valid guesses attempted: %d\n", data->thread_id, count);
    pthread_exit(NULL);
}


// Calculate the start and end points for a given length
void calculate_range(long long length, long long* start, long long* end) {
    if (length == 1) {
        *start = 1;
        *end = 5;
    } else if (length == 2) {
        *start = 7;  // base-6 "11"
        *end = 35;   // base-6 "55"
    } else {
        *start = 1 * binPow(6, length - 1) + 1;  // base-6 "1...1"
        *end = 0;
        for (long long i = 0; i < length; i++) {
            *end += 5 * binPow(6, i);
        }
    }
}


// void set_dock(int msgqid, int dockId) {
//     SolverRequest req;
//     req.mtype = SOLVER_MTYPE_SET_DOCK;
//     req.dockId = dockId; // Set individually before sending
//     memset(req.authStringGuess, 0, MAX_AUTH_STRING_LEN);
//     msgsnd(msgqid, &req, sizeof(SolverRequest) - sizeof(long), 0);
// }


// Main function to spawn threads and divide the search space
char* start_guessing(int length, int num_threads, int dockId) {
    long long start, end;
    char* found_guess = malloc(MAX_AUTH_STRING_LEN);
	found_guess[0] = '\0';


    calculate_range(length, &start, &end);
    printf("Global start : %d, Global end : %d\n", start, end);
    long long range = end - start + 1;
    long long chunk_size = range / num_threads;


    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];


    for (int i = 0; i < num_threads; i++) {
        long long chunk_start = start + i * chunk_size;
        long long chunk_end = (i == num_threads - 1) ? end : chunk_start + chunk_size - 1;


        thread_data[i].start = chunk_start;
        thread_data[i].end = chunk_end;
        thread_data[i].length = length;
        thread_data[i].thread_id = i;
        thread_data[i].msgqid = solverQueues[i];
        thread_data[i].dockId = dockId;
        thread_data[i].correct_guess = found_guess;
        // Set Dock
        SolverRequest req;
        req.mtype = SOLVER_MTYPE_SET_DOCK;
        req.dockId = dockId;
        memset(req.authStringGuess, 0, MAX_AUTH_STRING_LEN);
        msgsnd(solverQueues[i], &req, sizeof(SolverRequest) - sizeof(long), 0);

        // Start spawining threads
        // printf("Creating Thread with start : %d and end %d on solver %d\n", chunk_start, chunk_end, i);
        pthread_create(&threads[i], NULL, guess_generator, (void*)&thread_data[i]);
    }


    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    found = 0;
    return found_guess;
}
