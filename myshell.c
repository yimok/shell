#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#define EOL       1
#define ARG       2
#define AMPERSAND 3
#define SEMICOLON 4
#define PIPE      5


#define MAXARG    512
#define MAXBUF    512

#define FOREGROUND  0
#define BACKGROUND  1


int fatal(char *s);
int userin(char *p);
int gettok(char **outptr);
int inarg(char c);
int procline(void);
int runcommand(char **cline, int where);
int mypipe(int num, int where);


/*
ctrl + \ : SIGQUIT, coredump를 남기고 프로세스 종료
ctrl + z : SIGSTP , 해당프로세스 중지
ctrl + c : SIGINT , 프로세스를 종료시킨다.
*/


//프롬프트 문자
char *prompt = "Command> "; 

//프로그램 버퍼 및 작업용 포인터들 
static char inpbuf[MAXBUF], tokbuf[2 * MAXBUF], *ptr = inpbuf, *tok = tokbuf;

//파이프를 쓰기위한 포인터 변수
static char *piptemp[10][MAXARG + 1];


//인자로 받은 문자열을 출력하고(Command>) 
//키보드 문자를 한자씩 받는다. 
int userin(char *p)
{
	int c, count;

	//시그널을 막기위해 선언한 변수.
	static struct sigaction act3;

	//이전 루틴 수행한것을 지움
	ptr = inpbuf;
	tok = tokbuf;

	//command> 화면에 출력
	printf("%s", p);

	count = 0;

	while (1)
	{

		//쉘은 SIGINT(ctrl + c) , SIGQUIT(ctrl + \)로 종료가 되면 안되므로
		//SIG_IGN를 핸들러에 넣고 sigaction 함수로 지정된 시그널 봉쇄
		act3.sa_handler = SIG_IGN;
		sigaction(SIGINT, &act3, NULL);
		sigaction(SIGQUIT, &act3, NULL);

		//getchar 함수로 한글자씩 입력받아 변수 c에 저장 
		//만약 입력받은문자가 EOF이면 EOF 를 리턴하고 반복문 탈출
		if ((c = getchar()) == EOF)
			return (EOF);

		//count변수는 입력받을 버퍼의 크기를 체크하기위한 변수
		//MAXBUF 512보다 count가 작을경우 count++ 인덱스를 하나 증가시키고 
		//getchar로 받은 한 문자를 inpbuf에 저장,
		if (count < MAXBUF)
			inpbuf[count++] = c;

		//만약 입력받은 문자가 '\n' 행변환이면 실제 쉘에서 엔터를 친것이므로
		//inpbuf에 널문자 추가하여 문자열의 끝을 알림
		//정상적으로 수행했으므로 count를 리턴
		if (c == '\n' && count < MAXBUF)
		{
			inpbuf[count] = '\0';
			return count;
		}

		// 엔터를 눌렀는데 MAXBUF를 초과한다면
		// 입력한 문자가 너무많다고 출력해주고 다시 반복문 시작
		if (c == '\n')
		{
			printf("myshell: input line too long!\n");
			count = 0;
			printf("%s", p);
		}
	}
}

//userin이 구축한 명령줄로부터 개별적인 토큰을 추출한다.
int gettok(char **outptr)
{
	int type;
	int c;

	//outptr 문자열을 tok로 지정한다. 
	//outptr = tok = tokbuf
	*outptr = tok;


	/// 토큰을 포함하고 있는 버퍼로부터 여백을 제거
	while (*ptr == ' ' || *ptr == '\t')
		ptr++;

	// *ptr = inpbuf 
	// 토큰 포인터를 inpbuf 의 첫 번째 를 향하게 지정.
	*tok++ = *ptr;


	//ptr(버퍼)에 저장된 문자(토큰)에 따라 switch case 문 수행
	switch (*ptr++){


		//\n일 경우 줄의 끝을 알림	
	case '\n':
		type = EOL;
		break;

		//& 일경우 AMPERSAND라 알림, BACKGROUND로 실행할 것임
	case '&':
		type = AMPERSAND;
		break;

		//세미콜론일경우 type을 SEMICOLON으로 저장
	case ';':
		type = SEMICOLON;
		break;

		// |일 경우 type을 PIPE로 저장
	case '|':
		type = PIPE;
		break;

	default:
		type = ARG;
		//위에 제시된 토큰들을 거치지않을경우
		//inarg 함수로 제시된 특수문자들인지 판별하고
		//포인터를 하나 증가시켜 다음문자를 또 비교한다.(while문 반복)
		//지정된 특수문자일 경우 탈출
		while (inarg(*ptr))
			*tok++ = *ptr++;
	}

	*tok++ = '\0';
	return type;
}


//지정된 특수문자
static char special[] = { ' ', '\t', '&', ';', '\n', '\0' };

//쉘에서 특별히 쓰이는 문자인지 판별하는 함수.
int inarg(char c){
	char *wrk;
	//special 배열의 인덱스를 하나씩 증가시키면서
	//반복문을 수행하고 비교 후 리턴값 결정.
	for (wrk = special; *wrk; wrk++)
	{
		if (c == *wrk)
			return (0);
	}
	return (1);
}



//입력받은 라인을 처리하는 함수.
int procline(void)
{


	char *arg[MAXARG + 1];   	// runcommand 함수를 구동하기위한 arg 저장 변수
	int toktype;			    // 토큰의 유형을 나타내는 값을 포함하는 정수.
	int narg = 0;		            // 지금까지의 인수 수 
	int processCHK;		    	   // 프로세스가 foreground 인지 background인제 체크하기 위한 변수.
	int type = 0;
	int num = 0;                // 파이프 개수
	int i, j;
	char check[] = "exit";   //실행되는 쉘은 SIGINT, SIGQUIT를 막아두었으므로 exit 를 입력해 종료되도록 한다.


	//mypipe 문자를 저장할 변수 0으로 초기화
	for (i = 0; i < 10; i++)
	{
		for (j = 0; j < MAXARG + 1; j++)
			piptemp[i][j] = 0;
	}

	for (;;)
	{
		//gettok 함수를 호출하여 토큰타입을 저장
		//토큰의 타입에 따라 switch case문 수행
		//arg[narg] -> tok -> tokbuf
		switch (toktype = gettok(&arg[narg]))
		{

			//toktype이 ARG면 MAXARG 보나 작을경우 narg카운트를 하나씩증가시킴
			// 그리고 브레이크후 다시 gettok 하고 toktype 판별
		case ARG:
			if (narg < MAXARG)
				narg++;
			break;

			//toktype이 행문자나 세미콜론일 경우 
			// 그냥 아래로 내려감 
		case EOL:
		case SEMICOLON:

			//읽은 toktype이 앰퍼센트이면 백그라운드 프로세스인지
			//포그라운드 프로세스인지 판별한다.
			//EOL이나 세미콜론일경우도 이 구문을 통과함.
		case AMPERSAND:
			if (toktype == AMPERSAND)
				processCHK = BACKGROUND;
			else
				processCHK = FOREGROUND;


			//읽은 문자가 있을경우 아래구문 수행
			if (narg != 0)
			{


				arg[narg] = NULL;

				//입력한 첫 인자가 cd이면
				if (strcmp(arg[0], "cd") == 0)
				{
					//cd 뒤에 옵션이 있을경우
					if (narg != 1)
					{
						//chdir 함수를 수행하여 실패할경우
						if (chdir(arg[1]) == -1)
							printf("No such file or directory \n");
					}
					//없을경우
					else
					{
						//getenv 함수를 사용하여 HOME 환경변수의 이름을 인자로 넘겨 환경변수 값을 받고
						//이 값으로 chdir 수행 , 즉 cd 를 치면 지정된 HOME으로 이동
						chdir(getenv("HOME"));
					}
				}

				//커맨드에 PIPE를 사용하였을 경우
				else if (type == PIPE)
				{
					//static 전역변수로 선언된 piptemp 변수에 첫 커맨드의 arg(명령어)를 넣고
					//mypipe의 입력으로 사용하기위해 준비
					for (i = 0; i < narg; i++)
						piptemp[num][i] = arg[i];

					//몇개의 파이프를 만들것인지를 알려주는 num인자
					//백그라운드인지 포그라운드인지 check 하는 인자를 넘겨 mypipe 함수 호출
					mypipe(num, processCHK);
				}
				//mypipe 사용을 안한다면 일반적인 방법으로 커맨드 하나 수행
				else{
					//쉘은 시그널을 못받게했기때문에 exit로 종료가능
					if (strcmp(check, *arg) == 0)
						exit(1);

					//runcommand 수행
					runcommand(arg, processCHK);
				}
			}

			//eol을 만나면 정상적으로 한줄이 수행되었으므로 리턴하고 프롬프트를 다시 뛰움
			if (toktype == EOL)
				return 0;

			narg = 0;
			break;


			//toktype이 PIPE이면 piptemp를 구성한다.
			//몇개의 파이프를 만들것인지, 무슨 입력을 넘길것인지 결정
		case PIPE:
			// 파이프 문자열을 저장   
			for (i = 0; i < narg; i++)
				piptemp[num][i] = arg[i];
			piptemp[num][narg] = NULL;
			num++;
			narg = 0;
			type = toktype;
			break;


		}
	}
}

//일반적인 하나의 커맨드 수행(파이프 사용 x)
int runcommand(char **cline, int where)
{
	pid_t pid;
	int status;
	static struct sigaction act;


	//fork 시스템콜 호출하여 프로세스 생성
	switch (pid = fork())
	{
	case -1:
		perror("runcommand error");
		return (-1);

		//자식일경우에
	case 0:
		//백그라운드 실행시에 SIG_INT , SIG_QUIT 막음
		if (where == BACKGROUND)
		{
			act.sa_handler = SIG_IGN;
			sigaction(SIGINT, &act, NULL);
			sigaction(SIGQUIT, &act, NULL);

		}
		//백그라운드가 아니면  SIG_INT , SIG_QUIT 로 종료가능
		else{

			act.sa_handler = SIG_DFL;
			sigaction(SIGINT, &act, NULL);
			sigaction(SIGQUIT, &act, NULL);


		}

		//execvp는 수행가능한 화일의 화일이름만 필요로한다.
		//첫번째인자에는 실행시킬 명령문(ls , cat 등..)
		//두번째인자는 같이넘길 arguments(배열형식으로 넘김)
		execvp(*cline, cline);
		perror(*cline);
		exit(1);

		//부모일경우
	default:

		act.sa_handler = SIG_IGN;
		sigaction(SIGINT, &act, NULL);
		sigaction(SIGQUIT, &act, NULL);
		sigaction(SIGTSTP, &act, NULL);


	}
	// 만일 백그라운드 프로세스이면 프로세스 식별자를 프린트하고 퇴장한다. 
	if (where == BACKGROUND)
	{
		printf("[Process id %d]\n", pid);
		return (0);
	}

	//백그라운드가 아니면 자식이 끝날떄까지 기다린다.
	if (waitpid(pid, &status, 0) == -1)
		return (-1);
	else
		return (status);
}



int mypipe(int num, int where)
{
	pid_t pid[num + 1];
	int i, j;
	int pip[num][2];
	int status;
	static struct sigaction act2;

	//넘겨받은 인자 num 만큼 파이프를 for문을 이용해 생성한다.
	for (i = 0; i < num; i++)
	{
		if (pipe(pip[i]) == -1)
			fatal("mypipe error");
	}

	//num에 맞게 프로세스 fork() 수행
	for (i = 0; i <= num; i++)
	{
		//fork 시스템 콜 수행
		//성공 시, 자식 프로세스는 0이며, 부모 프로세스는 -1과 0 이외의 값을 가짐
		switch (pid[i] = fork()){
		case -1:
			fatal("fork error");

			//자식 이라면
		case 0:
			//백그라운드일경우 SIG_INT, SIG_QUIT , ignore 시킴
			if (where == BACKGROUND)
			{
				act2.sa_handler = SIG_IGN;
				sigaction(SIGINT, &act2, NULL);
				sigaction(SIGQUIT, &act2, NULL);

			}
			//포그라운드라면 시그널로 종료가능
			else
			{
				act2.sa_handler = SIG_DFL;
				sigaction(SIGINT, &act2, NULL);
				sigaction(SIGQUIT, &act2, NULL);

			}



			//파일디스크립터 0 은 stdin 
			//파일디스크립터 1 은 stdout

			// 첫 번째 커맨드에서의 출력이 첫 파이프의 입력으로 들어오고
			// 첫파이프의 출력이 다음 커맨드의 입력으로 들어간다.
			if (i == 0)
			{   // dup2는 시스템 콜 함수
				// dup2(p[i][1],1);이 호출된 이후에는 
				// 모든출력(stdout)이 pip[i][1]파일디스크립터 로 향하게 된다.
				// 즉 첫 커맨드의 출력이 첫 파이프의 입력으로 들어가게 된다.
				dup2(pip[i][1], 1);
				for (j = 0; j < num; j++)
				{
					//파이프 쓰기 읽기 봉쇄
					close(pip[j][1]);
					close(pip[j][0]);
				}

				//execvp로 piptemp에 저장된 명령어 , 추가 아규먼트를 넘겨 새 프로그램 수행
				execvp(piptemp[i][0], piptemp[i]);
				fatal(piptemp[i][0]);
			}
			//마지막 파이프를 다루는 구문
			else if (i == num)
			{

				// dup2는 시스템 콜 함수
				// dup2(pip[i - 1][0],0);이 호출된 이후에는 
				// 모든입력(stdin)이 pip[i - 1][0]로 향하게 된다.
				dup2(pip[i - 1][0], 0);
				for (j = 0; j < num; j++)
				{
					//파이프 쓰기 읽기 봉쇄
					close(pip[j][0]);
					close(pip[j][1]);
				}

				//execvp로 piptemp에 저장된 명령어 , 추가 아규먼트를 넘겨 새프로그램 수행
				execvp(piptemp[num][0], piptemp[num]);
				fatal(piptemp[num][0]);
			}
			// 그이외 사이 파이프
			else
			{
				// stdin, stout을 양쪽에 향하게 하여 파이프 입출력 구현
				dup2(pip[i - 1][0], 0);
				dup2(pip[i][1], 1);
				for (j = 0; j < num; j++)
				{
					//파이프 쓰기 읽기 봉쇄
					close(pip[j][0]);
					close(pip[j][1]);
				}
				//execvp로 piptemp에 저장된 명령어 , 추가 아규먼트를 넘겨 새프로그램 수행
				execvp(piptemp[i][0], piptemp[i]);
				fatal(piptemp[i][0]);
			}
		}
	}
	// 부모 프로세스 
	for (j = 0; j < num; j++)
	{
		close(pip[j][0]);
		close(pip[j][1]);
	}

	// BACKGROUND의 경우 자식 프로세스 아이디 출력. 
	if (where == BACKGROUND)
	{
		for (j = 0; j <= num; j++)
		{
			if (pid[j] > 0)
				printf("[Process id %d]\n", pid[j]);
			else
				sleep(1);
		}
		return(0);
	}

	//백그라운드가 아니면 자식이 끝날떄까지 기다린다.
	while (waitpid(pid[num], &status, WNOHANG) == 0)
		sleep(1);
	return(0);
}


int fatal(char *s)
{
	perror(s);
	exit(1);
}




int main()
{



	while (userin(prompt) != EOF)
	{


		procline();


	}


	return 0;
}



