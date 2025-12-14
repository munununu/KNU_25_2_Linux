#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <time.h>

#define PROCESS_NUM 10
#define TIME_QUANTUM 1
#define CS_OVERHEAD 1

enum State { READY, RUNNING, SLEEP, DONE };

struct msg_buffer {
    long msg_type;
    int pid_idx;
    int event_type; // 1: I/O, 2: TERMINATE
};

typedef struct {
    pid_t pid;
    int remaining_quantum;
    enum State state;
    int io_wait_remain;
    int ready_entry_tick;
    int total_wait_tick;
} PCB;

PCB pcb_table[PROCESS_NUM];
int sim_time = 0;
int active_process_cnt = PROCESS_NUM;
int current_proc_idx = -1;

// --- 자식 프로세스 ---
void child_process(int idx, int msgid) {
    srand(time(NULL) ^ (getpid() << 16));
    int cpu_burst = (rand() % 10) + 1;

    // [디버깅] 자식 생성 확인
    printf("[Child %d] ALIVE! PID: %d, Burst: %d\n", idx, getpid(), cpu_burst);

    // 시그널 셋 설정 (이미 Main에서 Block 했으므로 여기선 풀기 위한 셋만 준비)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    int sig;

    struct msg_buffer msg;
    msg.msg_type = 1;
    msg.pid_idx = idx;

    while (1) {
        // 부모 시그널 대기 (여기서 잠깐 Unblock 되고 시그널 받음)
        int ret = sigwait(&mask, &sig);
        if (ret != 0) {
            perror("sigwait failed");
            exit(1);
        }

        // 실행 (CPU Burst 감소)
        cpu_burst--;

        // 종료 조건
        if (cpu_burst <= 0) {
            msg.event_type = 2; // TERMINATE
            if (msgsnd(msgid, &msg, sizeof(struct msg_buffer) - sizeof(long), 0) == -1) {
                perror("msgsnd error");
            }
            exit(0); // 정상 종료
        }

        // I/O 요청
        if ((rand() % 10) < 3) {
            msg.event_type = 1; // I/O REQUEST
            if (msgsnd(msgid, &msg, sizeof(struct msg_buffer) - sizeof(long), 0) == -1) {
                perror("msgsnd error");
            }
        }
    }
}

// --- 부모 프로세스 ---
void parent_kernel(int msgid) {
    struct msg_buffer rcv_msg;

    // PCB 초기화
    for (int i = 0; i < PROCESS_NUM; i++) {
        pcb_table[i].remaining_quantum = TIME_QUANTUM;
        pcb_table[i].state = READY;
        pcb_table[i].ready_entry_tick = 0;
        pcb_table[i].total_wait_tick = 0;
    }

    printf("\n=== Kernel Simulation Start (Quantum: %d) ===\n", TIME_QUANTUM);

    while (active_process_cnt > 0) {
        usleep(50000); // 0.05초
        sim_time++;

        // 1. Sleep 큐 관리
        for (int i = 0; i < PROCESS_NUM; i++) {
            if (pcb_table[i].state == SLEEP) {
                pcb_table[i].io_wait_remain--;
                if (pcb_table[i].io_wait_remain <= 0) {
                    pcb_table[i].state = READY;
                    pcb_table[i].ready_entry_tick = sim_time;
                    printf("[Tick %d] Proc %d: Wakeup -> READY\n", sim_time, i);
                }
            }
        }

        // 2. 현재 프로세스 퀀텀 체크
        if (current_proc_idx != -1) {
            pcb_table[current_proc_idx].remaining_quantum--;
            if (pcb_table[current_proc_idx].remaining_quantum <= 0) {
                printf("[Tick %d] Proc %d: Quantum Expired -> READY\n", sim_time, current_proc_idx);
                pcb_table[current_proc_idx].state = READY;
                pcb_table[current_proc_idx].ready_entry_tick = sim_time;
                current_proc_idx = -1;
            }
        }

        // 3. 퀀텀 리셋 로직
        int can_run_exists = 0;
        for (int i = 0; i < PROCESS_NUM; i++) {
            if (pcb_table[i].state == READY && pcb_table[i].remaining_quantum > 0) {
                can_run_exists = 1;
                break;
            }
        }
        if (current_proc_idx == -1 && !can_run_exists) {
            int reset_cnt = 0;
            for(int i=0; i<PROCESS_NUM; i++){
                 if(pcb_table[i].state != DONE) {
                     pcb_table[i].remaining_quantum = TIME_QUANTUM;
                     reset_cnt++;
                 }
            }
            if(reset_cnt > 0) printf("[Tick %d] >>> Reset Quantums <<<\n", sim_time);
        }

        // 4. 스케줄링 (Round Robin)
        if (current_proc_idx == -1) {
            static int search_start = 0;
            for (int i = 0; i < PROCESS_NUM; i++) {
                int idx = (search_start + i) % PROCESS_NUM;
                
                // 스케줄링 하려는 애가 진짜 살아있나 확인
                // kill(pid, 0)은 시그널을 보내지 않고 존재 여부만 확인
                if (pcb_table[idx].state != DONE && kill(pcb_table[idx].pid, 0) == -1) {
                    printf("[Tick %d] Proc %d (PID %d) is DEAD unexpectedly! Cleaning up...\n", sim_time, idx, pcb_table[idx].pid);
                    pcb_table[idx].state = DONE;
                    waitpid(pcb_table[idx].pid, NULL, WNOHANG);
                    active_process_cnt--;
                    continue; // 죽은 애는 건너뛰기
                }

                if (pcb_table[idx].state == READY && pcb_table[idx].remaining_quantum > 0) {
         	   
		    if (CS_OVERHEAD > 0) {
                        printf("[Tick %d] !!! Context Switch Overhead (%d ticks) !!!\n", sim_time, CS_OVERHEAD);
                        
                        // 오버헤드 시간만큼 강제로 시간을 흘려보냄
                        for (int o = 0; o < CS_OVERHEAD; o++) {
                            sim_time++; 
                            
                            // 시간이 흐르는 동안 Sleep 중인 프로세스 처리 (I/O는 계속 진행)
                            for (int k = 0; k < PROCESS_NUM; k++) {
                                if (pcb_table[k].state == SLEEP) {
                                    pcb_table[k].io_wait_remain--;
                                    if (pcb_table[k].io_wait_remain <= 0) {
                                        pcb_table[k].state = READY;
                                        pcb_table[k].ready_entry_tick = sim_time; // 깨어난 시간 기록
                                        printf("[Tick %d] Proc %d: Wakeup -> READY (during overhead)\n", sim_time, k);
                                    }
                                }
                            }
                        }
                    }
	
		    current_proc_idx = idx;
                    search_start = (idx + 1) % PROCESS_NUM;
                    
                    int wait = sim_time - pcb_table[current_proc_idx].ready_entry_tick;
                    pcb_table[current_proc_idx].total_wait_tick += wait;
                    pcb_table[current_proc_idx].state = RUNNING;
                    
                    printf("[Tick %d] Scheduler: Proc %d Selected\n", sim_time, current_proc_idx);
                    break;
                }
            }
        }

        // 5. 실행 및 메시지 수신
        if (current_proc_idx != -1) {
            // 시그널 전송
            kill(pcb_table[current_proc_idx].pid, SIGUSR1);

            // 메시지 확인 (여러 개가 쌓여있을 수 있으므로 while로 모두 처리)
            while(msgrcv(msgid, &rcv_msg, sizeof(struct msg_buffer) - sizeof(long), 1, IPC_NOWAIT) != -1) {
                int sender = rcv_msg.pid_idx;
                if (rcv_msg.event_type == 1) { // I/O
                    pcb_table[sender].state = SLEEP;
                    pcb_table[sender].io_wait_remain = (rand() % 5) + 1;
                    printf("[Tick %d] Proc %d: I/O Request -> SLEEP\n", sim_time, sender);
                    if (sender == current_proc_idx) current_proc_idx = -1;
                } 
                else if (rcv_msg.event_type == 2) { // Terminate
                    pcb_table[sender].state = DONE;
                    printf("[Tick %d] Proc %d: Finished -> DONE\n", sim_time, sender);
                    waitpid(pcb_table[sender].pid, NULL, 0);
                    active_process_cnt--;
                    if (sender == current_proc_idx) current_proc_idx = -1;
                }
            }
        }
    }

    printf("\n=== Simulation Finished ===\n");
    // 성능 분석 출력
    printf("\n[Performance Analysis - Time Quantum: %d]\n", TIME_QUANTUM);
    double total_avg_wait = 0;
    for (int i = 0; i < PROCESS_NUM; i++) {
        printf("Proc %d: Wait = %d ticks\n", i, pcb_table[i].total_wait_tick);
        total_avg_wait += pcb_table[i].total_wait_tick;
    }
    printf(">> Average Waiting Time: %.2f ticks\n", total_avg_wait / PROCESS_NUM);

    msgctl(msgid, IPC_RMID, NULL);
}

int main() {
    int msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    
    // 시그널 블록 (부모에서 미리 막아야 자식이 안 죽음)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL); // 부모 + 미래의 자식들 보호

    for (int i = 0; i < PROCESS_NUM; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_process(i, msgid);
            exit(0);
        } else {
            pcb_table[i].pid = pid;
        }
    }

    parent_kernel(msgid);
    return 0;
}
