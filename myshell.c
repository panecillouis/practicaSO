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

void redirigir_archivo(char *archivo, int tipo) {
	//Descriptor de fichero para interactuar con el archivo
	//En cuanto a los permisos: 
	//En caso de lectura, se abre el archivo en modo lectura.
	//En caso de escritura: se abrira el archivo en modo escritura, se creara si no existe, se vacia si ya existe.
	//0644 es el valor de los permisos que se asignaran al archivo: lectura y escritura para el dueño, lectura para el grupo>
    int fd;
    if (tipo == STDIN_FILENO) {
        fd = open(archivo, O_RDONLY);
    } else if (tipo == STDOUT_FILENO || tipo == STDERR_FILENO) {
        fd = open(archivo, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    }
	
    if (fd < 0) {
        fprintf(stderr, "Error al abrir el archivo '%s' para %s: %s\n", 
                archivo, (tipo == STDIN_FILENO ? "lectura" : (tipo == STDOUT_FILENO ? "escritura" : "error")), strerror(errno));
        exit(1);
    }
	//Se redirige la entrada estándar hacia el archivo
    dup2(fd, tipo);

	// Se cierra el descriptor de fichero
    close(fd);
}

void ejecutar_comandos(tline *line) {
    int status;
    if (line->ncommands == 1) {
        pid_t pid = fork();
        if (pid == 0) {
            // Redirección de entrada, salida y error si están especificados

            if (line->redirect_input) redirigir_archivo(line->redirect_input, STDIN_FILENO);
            if (line->redirect_output) redirigir_archivo(line->redirect_output, STDOUT_FILENO);
            if (line->redirect_error) redirigir_archivo(line->redirect_error, STDERR_FILENO);
            if (line->commands[0].argv[0] == NULL) {
                fprintf(stderr, "Error: No se proporcionó ningún comando para ejecutar.\n");
                exit(1);
            }

            if (execvp(line->commands[0].argv[0], line->commands[0].argv) == -1) {
                fprintf(stderr, "Error al ejecutar el comando '%s': %s\n", 
                        line->commands[0].argv[0], strerror(errno));
                exit(1);
            }
        } else {
            wait(&status);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                printf("El comando no se ejecutó correctamente\n");
        }
    } else {
        int num_pipes = line->ncommands - 1;
        int fd[num_pipes][2];
        for (int i = 0; i < num_pipes; i++) pipe(fd[i]);

        for (int i = 0; i < line->ncommands; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                if (i == 0 && line->redirect_input) redirigir_archivo(line->redirect_input, STDIN_FILENO);
                if (i > 0) dup2(fd[i - 1][READ_END], STDIN_FILENO);
                if (i == line->ncommands - 1 && line->redirect_output) redirigir_archivo(line->redirect_output, STDOUT_FILENO);
				if (i == line->ncommands - 1 && line->redirect_error) redirigir_archivo(line->redirect_error, STDERR_FILENO); 
                if (i < line->ncommands - 1) dup2(fd[i][WRITE_END], STDOUT_FILENO);

	            // Cerrar todos los pipes no usados
                for (int j = 0; j < num_pipes; j++) {
                    close(fd[j][READ_END]);
                    close(fd[j][WRITE_END]);
                }

				// Ejecutar el comando
                execvp(line->commands[i].argv[0], line->commands[i].argv);
                printf("Error al ejecutar el comando: %s\n", strerror(errno));
                exit(1);
            }
        }
        for (int i = 0; i < num_pipes; i++) {
            close(fd[i][READ_END]);
            close(fd[i][WRITE_END]);
        }
        for (int i = 0; i < line->ncommands; i++) wait(&status);
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