/* 
 * tsh - A tiny shell program with job control
 * 
 * <문요준 yojun313>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *cmd_args[MAXARGS];   // 명령어 인자 저장 배열
    int  is_background;        // 백그라운드 실행 여부 플래그 변수

    sigset_t signal_mask;      // 시그널 마스크 저장 변수
    pid_t    process_id;       // 자식 프로세스 ID 저장 변수

    // 명령어 파싱 및 백그라운드 여부 확인
    is_background = parseline(cmdline, cmd_args);

    // 명령어 비어 있으면 종료
    if (cmd_args[0] == NULL) return;

    // 빌트인 명령어가 아니면 실행
    if (!builtin_cmd(cmd_args))
    {
        // 시그널 마스크 설정 (SIGCHLD 차단)
        sigemptyset(&signal_mask);                   // 시그널 마스크 초기화
        sigaddset(&signal_mask, SIGCHLD);            // 시그널 마스크에 SIGCHLD 추가
        sigprocmask(SIG_BLOCK, &signal_mask, NULL);  // 부모 프로세스가 SIGCHLD 차단 (자식 프로세스 생성 및 실행 동안 부모 프로세스 방해 금지 목적)


        // 프로세스 생성 실패
        if ((process_id = fork()) < 0)
        {
            return;
        }

        // 프로세스 생성
        else if (process_id == 0)
        {
            // 자식 프로세스
            sigprocmask(SIG_UNBLOCK, &signal_mask, NULL);  // 시그널 차단 해제
            setpgid(0,0);                                  // 자식 프로세스를 독립된 프로세스 그룹으로 설정 -> 백그라운드/포그라운드 작업 구분

            // 명령어 실행
            if (execve(cmd_args[0], cmd_args, environ) < 0)     // 프로그램 경로, 인자 배열, 환경 변수
            {
                printf("%s: Command not found\n", cmd_args[0]); // 에러메시지 출력
                exit(0);
            }
        }
        
        // 부모 프로세스
        else
        {
            sigprocmask(SIG_UNBLOCK, &signal_mask, NULL); // 시그널 차단 해제
            
            // 포그라운드 실행
            if (!is_background)
            {
                addjob(jobs, process_id, FG, cmdline);    // 작업 추가
                waitfg(process_id);                       // 자식 프로세스가 종료될 때까지 대기
            }
            // 백그라운드 실행
            else
            {
                addjob(jobs, process_id, BG, cmdline);    // 작업 추가
                printf("[%d] (%d) %s", pid2jid(process_id), process_id, cmdline);
            }
        }
        return;
    }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    char *command  = argv[0];  // 첫 번째 인자에서 명령어 추출
    int is_builtin = 1;        // 내장 명령어 여부 플래그

    // 내장 명령어 확인
    if (!strcmp(command, "quit"))
    {
        exit(0);               // 프로그램 종료
    }
    else if (!strcmp(command, "jobs"))
    {
        listjobs(jobs);        // 현재 작업 목록 출력
    }
    else if (!strcmp(command, "bg") || !strcmp(command, "fg"))
    {
        do_bgfg(argv);    // 백그라운드 또는 포그라운드 작업 처리
    }
    else
    {
        is_builtin = 0;        // 내장 명령어가 아님
    }

    // 내장 명령어인 경우 1 반환
    if (is_builtin)
    {
        return 1;
    }

    // 내장 명령어가 아닌 경우 0 반환
    return 0;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    struct job_t *job = NULL; // 작업의 상태, ID 등을 저장하는 구조체 포인터
    pid_t process_id;         // 프로세스 ID
    int job_id;               // 작업 ID

    char *command = argv[0];  // 명령어 (fg or bg)
    char *target  = argv[1];  // 대상 (PID or JID)

    // 인자가 제공되지 않은 경우 에러 출력
    if (target == NULL)
    {
        printf("%s command requires PID or %%jobid argument\n", command);
        return;
    }

    // PID 또는 JID 확인
    if (target[0] != '%')
    {
        if (!isdigit(target[0]))
        {
            // 입력이 %로 시작하지 않고 숫자가 아닐 경우 에러 메시지 출력
            printf("%s: argument must be a PID or %%jobid\n", command);
            return;
        }
    }

    // JID로 처리
    if (target[0] == '%')
    {
        job_id = atoi(&target[1]); // '%' 뒤의 숫자를 정수로 변환

        // 유효하지 않은 JID
        if (job_id == 0)
        {
            printf("%s: argument must be a PID or %%jobid\n", command);
            return;
        }

        // 작업 배열 jobs에서 JID에 해당하는 작업 구조체 가져오기
        job = getjobjid(jobs, job_id);

        // 해당 JID 작업이 없을 경우
        if (job == NULL)
        {
            printf("%s: No such job\n", target);
            return;
        }
    }
    // PID로 처리
    else if (isdigit(target[0]))
    {
        process_id = atoi(target); // 문자열을 정수(PID)로 변환
        
        // 유효하지 않은 PID
        if (process_id == 0)
        {
            printf("%s: argument must be a PID or %%jobid\n", command);
            return;
        }

        // 작업 배열 jobs에서 PID에 해당하는 작업 구조체 가져오기
        job = getjobpid(jobs, process_id);

        // 해당 PID 작업이 없을 경우
        if (job == NULL)
        {
            printf("(%s): No such process\n", target);
            return;
        }
    }
    // 명령어에 따라 동작 수행
    if (!strcmp(command, "fg")) 
    { 
        // 작업을 포그라운드로 전환
        job->state = FG;                                          // 상태를 FG로 설정
        kill(-job->pid, SIGCONT);                                 // SIGCONT 시그널 전송 -> 작업 재개
        waitfg(job->pid);                                         // 포그라운드 작업이 완료될 때까지 대기
    } 
    else if (!strcmp(command, "bg")) { 
        // 작업을 백그라운드로 전환
        job->state = BG;                                          // 상태를 BG로 설정
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline); // 작업 정보 출력
        kill(-job->pid, SIGCONT);                                 // SIGCONT 시그널 전송 -> 작업 재개
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    // 유효한 PID인지 확인
    if (pid == 0) 
    {
        // PID가 유효하지 않으면 종료
        return;
    }

    // 현재 포그라운드 프로세스가 종료될 때까지 대기
    while (fgpid(jobs) == pid) 
    {
        // 1초 동안 대기하며 반복
        sleep(1); 
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    pid_t child_pid;  // 자식 프로세스의 PID
    int child_status; // 자식 프로세스의 종료 상태 저장 변수

    // 종료되거나 중단된 모든 자식 프로세스를 처리
    while ((child_pid = waitpid(-1, &child_status, WNOHANG | WUNTRACED)) > 0) // waitpid 양수 반환: 상태 처리가 필요한 자식 프로세스 존재
    {
        // 자식 프로세스가 중단된 경우
        if (WIFSTOPPED(child_status))
        {
            struct job_t *job = getjobpid(jobs, child_pid); // PID를 기준으로 작업 목록(jobs)에서 해당 작업 검색

            // 작업이 존재하면
            if (job != NULL) 
            {
                job->state = ST; // 상태를 ST(Stopped)로 변경
            }
            // 중단된 작업의 JID, PID, 시그널 번호(WSTOPSIG)를 출력
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(child_pid), child_pid, WSTOPSIG(child_status)); 
        }
        
        // 자식 프로세스가 종료된 경우
        else if (WIFSIGNALED(child_status) || WIFEXITED(child_status))
        {
            // 자식 프로세스가 정상 종료되었으면
            if (WIFSIGNALED(child_status)) 
            {
                // 종료된 작업 정보 출력
                printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(child_pid), child_pid, WTERMSIG(child_status));
            }
            deletejob(jobs, child_pid); // 작업 목록에서 제거
        }
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    // 작업 배열(jobs)에서 현재 포그라운드 프로세스의 PID 가져옴
    pid_t foreground_pid = fgpid(jobs);

    // 유효한 PID가 있는 경우(포그라운드 작업이 있는 경우)
    if (foreground_pid > 0) 
    {
        // 프로세스(포그라운드) 그룹에 시그널 전달
        kill(-foreground_pid, sig); 
    }

    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    // 작업 배열(jobs)에서 현재 포그라운드 프로세스의 PID 가져옴
    pid_t foreground_pid = fgpid(jobs);

    // 유효한 PID가 있는 경우(포그라운드 작업이 없는 경우)
    if (foreground_pid > 0) 
    {
        // 프로세스(포그라운드) 그룹에 시그널 전달
        kill(-foreground_pid, SIGTSTP);
    }

    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



