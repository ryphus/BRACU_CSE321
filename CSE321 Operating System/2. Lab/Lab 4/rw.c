#include<stdio.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>

int main(){
	int fd;
	char buffer[80];
	char message[] = "Hello, world";
	
	fd = open("file1.txt",O_RDWR);
	printf("File 1 descriptor: %d\n",fd);
	
	//fd = open("file2.txt",O_RDWR);
	//printf("File 1 descriptor: %d\n",fd);
	
	//fd = open("file2.txt",O_RDWR|O_CREAT,0777);
	
	//write(0,message,sizeof(message));
	
	/**
	if (fd != -1){
		printf("myfile opened for read/write access\n");
		write(fd, message, sizeof(message));
		lseek(fd, 0, 0);
		read(fd, buffer, sizeof(message));
		printf("%s was written to myfile \n", buffer);
		close (fd);
	}
	**/
}
