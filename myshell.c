#include <stdio.h> //Para la entrada estándar
#include <stdlib.h> //exit, malloc, free, atoi...
#include "parser.h"  //Analizar mandatos
#include <sys/types.h> //Tipos de datos para la programaccción a nivel de sistema 
#include <sys/wait.h> //Control de procesos hijos y su estado de salida: wait, WIFEXITED...
#include <unistd.h> //funciones para llamadas al sistema: fork, execvp, dup2, close...
#include <errno.h>   //Para definir códigos de error para funciones del sistema
#include <string.h> //Para cadaenas de texto
#include <fcntl.h> //Para manipular archivos a nivel de sistema: open, O_RDONLY...

#define READ_END 0
#define WRITE_END 1
void redirigir_archivo(char * archivo, int tipo)
{	
	//Descriptor de fichero para interactuar con el archivo
	int fd;
	if (tipo == STDIN_FILENO)
	{
		fd = open(archivo, O_RDONLY);
		if(fd < 0)
		{
			printf("Error al abrir el archivo de entrada:%s\n", strerror(errno));
			exit(1);
		}
		//Se redirige la entrada estándar hacia el archivo
		dup2(fd, STDIN_FILENO);
	} else if(tipo == STDOUT_FILENO){
               	//La flag O_WRONLY abre el archivo en modo escritura.
             	//La flag O_CREAT crea el archivo si no existe.
                //La flag O_TRUNC vacía el archivo si existe, antes de escribir en él.
		//0644 es el valor de los permisos que se asignaran al archivo: lectura y escritura para el dueño, lectura para el grupo>
		fd = open(archivo, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if(fd < 0)
		{
			printf("Error al abrir el archivo de salida:%s\n", strerror(errno));
                        exit(1);
		}
		//Se redirige la salida estándar hacia el archivo
		dup2(fd, STDOUT_FILENO);
	}
	//Se cierra el descriptor de fichero
	close(fd);
}
void ejecutar_comandos(tline * line)
{	if (line->ncommands == 1)
	{
		pid_t pid;
		int status;
		pid = fork();
		if(pid == 0)
		{
			if(line->redirect_input != NULL){
				redirigir_archivo(line->redirect_input, STDIN_FILENO);
			}
			if(line->redirect_output!=NULL)
			{
				redirigir_archivo(line->redirect_output, STDOUT_FILENO);
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
		}
	}
	else
	{
		pid_t pid;
		int fd1[2];
		int status;
		pipe(fd1);
		pid = fork();
		if(pid == 0)
		{		close(fd1[READ_END]);
			if(line->redirect_input != NULL){
				redirigir_archivo(line->redirect_input, STDIN_FILENO);
			}
			dup2(fd1[WRITE_END], STDOUT_FILENO);
			close(fd1[WRITE_END]);
			execvp(line->commands[0].argv[0], line->commands[0].argv);
			printf("Error al ejecutar el comando: %s\n", strerror(errno));
			exit(1);
		}
		else
		{
			close(fd1[WRITE_END]);
			pid = fork();
			if (pid == 0)
			{
				dup2(fd1[READ_END], STDIN_FILENO);
				close(fd1[READ_END]);
				if(line->redirect_output!=NULL)
               		 	{
                        		redirigir_archivo(line->redirect_output, STDOUT_FILENO);
                		}
			 	execvp(line->commands[1].argv[0], line->commands[1].argv);
				printf("Error al ejecutar el comando: %s\n", strerror(errno));			
				exit(1);
			}
			else
			{
				close(fd1[READ_END]);
			}
			wait(&status);
			wait(&status);
		}
	}
	return;
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
