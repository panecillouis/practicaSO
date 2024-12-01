#include <stdio.h> //Para la entrada estándar
#include <stdlib.h> //exit, malloc, free, atoi...
#include "parser.h"  //Analizar mandatos
#include <sys/types.h> //Tipos de datos para la programaccción a nivel de sistema 
#include <sys/wait.h> //Control de procesos hijos y su estado de salida: wait, WIFEXITED...
#include <unistd.h> //funciones para llamadas al sistema: fork, execvp, dup2, close...
#include <errno.h>   //Para definir códigos de error para funciones del sistema
#include <string.h> //Para cadaenas de texto
#include <fcntl.h> //Para manipular archivos a nivel de sistema: open, O_RDONLY...
void ejecutar_comandos(tline * line)
{
	pid_t pid;
	int status;
	pid = fork();
	if(pid == 0)
	{
		if(line->redirect_input != NULL){
			//Se devuelve un descriptor de fichero para interactuar con el archivo
			int input_fd = open(line->redirect_input, O_RDONLY);
			if(input_fd < 0){
				printf("Error al abrir el archivo de entrada");
				exit(1);
			}
			//Se redirige la entrada estándar hacia el archivo
			dup2(input_fd, STDIN_FILENO);
			//Se cierra el descriptor de fichero
			close(input_fd);
		}
		if(line->redirect_output!=NULL)
		{			
			//Se devuelve un descriptor de fichero para interactuar con el archivo
			//La flag O_WRONLY abre el archivo en modo escritura.
			//La flag O_CREAT crea el archivo si no existe.
			//La flag O_TRUNC vacía el archivo si existe, antes de escribir en él.
			//0644 es el valor de los permisos que se asignaran al archivo: lectura y escritura para el dueño, lectura para el grupo y lectura para usuarios. 
			int output_fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (output_fd < 0) {
				printf("Error al abrir el archivo de salida");
				exit(1);
			}
			//Se redirige la salida estándar
			dup2(output_fd, STDOUT_FILENO);
			//Se cierra el descriptor de fichero
			close(output_fd);
		}
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
