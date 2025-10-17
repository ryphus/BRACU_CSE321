#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>  
int main(){
	pid_t pid;
	pid = fork();
	if (pid == 0){
	printf("I'm the child process\n");
	}
	else if (pid > 0){
	printf("I'm the parent process. My child pid is %d\n", pid);
	}
	else{
	perror("error in fork");
	}
}
