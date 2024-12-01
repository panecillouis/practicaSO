#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

void ejecutar_comandos(tline * line)
{
	pid_t pid;
	int status;
	pid = fork();
	if(pid == 0)
	{
		execvp(line->commands[0].argv[0], line->commands[0].argv);
		printf("Error al ejecutar el comando: %s\n", strerror(errno));
		exit(1);
	}
	else
	{
		wait(&status);
		if (WIFEXITED(status) != 0) //si es != 0, mi hijo hizo el exit
			if (WEXITSTATUS(status) != 0)
				printf("El comando no se ejecutó correctamente\n");
		return;
	}
}
int main (void)
{
	char buf[1024];
	tline * line;
	printf("msh> ");
	while(fgets(buf, 1024, stdin)){
		line = tokenize(buf);
		if (line==NULL) {
			continue;
		}
		if (line!=NULL){
		ejecutar_comandos(line);
		}
		printf("msh> ");	
	}
	return 0;
}
