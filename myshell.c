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
#define MAX_JOBS 10

// Estructura para salida de jobs
typedef struct {
    pid_t pid;
    char *nombre;
    int estado; // 0: no iniciado, 1: ejecutando, 2: terminado
} tjob;

tjob jobs[MAX_JOBS];
int numJobs = 0;

//funcion para cd

void cd_comando(char *path) {
	if (path == NULL) {
		path = getenv("HOME");
		if (chdir(path) != 0) {
            fprintf(stderr, "Error al cambiar al directorio HOME: %s\n", strerror(errno));
        }
	}
	else {
		if (chdir(path) != 0) {
            fprintf(stderr, "Error al cambiar al directorio '%s': %s\n", path, strerror(errno));
		}
	}
}

//funcion para jobs
void jobs_comando() {
	if (numJobs == 0) {
		printf("No hay trabajos en segundo plano\n");
		return;
	}
	for (int i = 0; i < numJobs; i++) {
        printf("[%d] %d %s %s\n", i + 1, jobs[i].pid, (jobs[i].estado == 2 ? "terminado" : "ejecutando"), jobs[i].nombre);	
	}
}

//funcion fg
void fg_comando(int job) {
	if (job < 1 || job > numJobs) {
		fprintf(stderr, "Error: el trabajo especificado no existe\n");
		return;
	}
	pid_t pid = jobs[job - 1].pid;

	if(waitpid(pid, NULL, 0) == -1) {
		fprintf(stderr, "Error al esperar al trabajo en primer plano: %s\n", strerror(errno));
	} else {
        printf("Proceso %d terminado.\n", pid);
	}

    // Eliminar el trabajo de la lista
	for (int i = job - 1; i < numJobs - 1; i++) {
		jobs[i] = jobs[i + 1];
	}
	numJobs--;
}


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
	int bg = 0;

	// Verificar si el último comando es en segundo plano
    if (line->commands[line->ncommands - 1].argv[0] != NULL && 
        strcmp(line->commands[line->ncommands - 1].argv[0], "&") == 0) {
        bg = 1;
        line->commands[line->ncommands - 1].argv[0] = NULL;  // Eliminar el "&" de los argumentos
    }


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
     	else {
			if (bg) {
				jobs[numJobs].pid = pid;
				jobs[numJobs].nombre = line->commands[0].argv[0];
				jobs[numJobs].estado = 1;
				numJobs++;
                printf("Proceso en segundo plano con PID %d\n", pid);
			} else {
				waitpid(pid, &status, 0);
				if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
					printf("El comando no se ejecutó correctamente\n");
			}
		}
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
		// Cerramos los pipes en el proceso principal
        for (int i = 0; i < num_pipes; i++) {
            close(fd[i][READ_END]);
            close(fd[i][WRITE_END]);
        }
		if (!bg){
			for (int i = 0; i < num_pipes; i++) {
				wait(&status);
			}
		}
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
		if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "cd") == 0) {
			cd_comando(line->commands[0].argv[1]);
        } else if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "jobs") == 0) {
			jobs_comando();
		} else if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "fg") == 0) {
            fg_comando(atoi(line->commands[0].argv[1]));
        } else {
			ejecutar_comandos(line);
		}
		printf("msh> ");	
	}
	return 0;
}