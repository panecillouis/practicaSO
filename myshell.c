#include <stdio.h> //Para la entrada estándar
#include <stdlib.h> //exit, malloc, free, atoi...
#include "parser.h"  //Analizar mandatos
#include <sys/types.h> //Tipos de datos para la programaccción a nivel de sistema 
#include <sys/wait.h> //Control de procesos hijos y su estado de salida: wait, WIFEXITED...
#include <unistd.h> //funciones para llamadas al sistema: fork, execvp, dup2, close...
#include <errno.h>   //Para definir códigos de error para funciones del sistema
#include <string.h> //Para cadaenas de texto
#include <fcntl.h> //Para manipular archivos a nivel de sistema: open, O_RDONLY...
#include <signal.h> //Para manejar señales

#define READ_END 0 // Descriptor de lectura
#define WRITE_END 1 // Descriptor de escritura
#define MAX_JOBS 20 //Numero maximo de jobs almacenados 

// Estructura para salida de jobs
typedef struct {
    pid_t pid; //ID del proceso
    char *nombre; // Nombre del trabajo
    int estado; // 0: no iniciado, 1: ejecutando, 2: terminado
} tjob;

tjob jobs[MAX_JOBS];
int numJobs = 0;

//Con esta función y la libreria de signal evitamos que las señales cierren el proceso de la minishell
void configurar_signal_shell() {
    signal(SIGINT, SIG_IGN); // Ignorar SIGINT
    signal(SIGQUIT, SIG_IGN); // Ignorar SIGQUIT

}

//funcion para cd, le pasamos el parametro path como puntero para poder cambiar el directorio
void cd_comando(char *path) { 
	if (path == NULL) { //Si no se proporciona directorio
		path = getenv("HOME"); //Buscamos el directorio HOME mediante la variable de entorno HOME
		if (chdir(path) != 0) { //Si no es capaz de acceder al directorio HOME, se imprime el error
            fprintf(stderr, "Error al cambiar al directorio HOME: %s\n", strerror(errno));
        }
	}
	else {
		if (chdir(path) != 0) { // Cambiamos al directorio path proporcionado, sino se imprime el error
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
    // Recorremos los procesos en bg
    for (int i = 0; i < numJobs; i++) {
        // Verifica el estado del proceso sin bloquear la ejecucion del programa 
        // Si el hijo ha cambiado de estado a detenido o terminado devuelve el pid de ese proceso, sino devuelve 0
        pid_t result = waitpid(jobs[i].pid, NULL, WNOHANG | WUNTRACED);
        if (result == 0) {
            // El proceso sigue ejecutándose
            jobs[i].estado = 1; // Ejecutando
        } else {
            // El proceso ha terminado: WIFEXITED(ha terminado normalmente), WIFSIGNALED(ha terminado por señal SIGTERM)
            if (WIFEXITED(result) || WIFSIGNALED(result)) {
                jobs[i].estado = 2; // Terminado
            // El proceso ha sido detenido SIGTSTOP
            } else if (WIFSTOPPED(result)) {
                jobs[i].estado = 3; // Detenido
            }
        }
    }
	// Imprimir y limpiar trabajos
    int i = 0;
    while (i < numJobs) {
        printf("[%d] %d %s %s\n", i + 1, jobs[i].pid,
               (jobs[i].estado == 1 ? "ejecutando" :
                (jobs[i].estado == 2 ? "terminado" : "detenido")),
               jobs[i].nombre);

        // Eliminar trabajos terminados
        if (jobs[i].estado == 2) { // Si está terminado
            free(jobs[i].nombre);  // Liberar memoria
            for (int j = i; j < numJobs - 1; j++) {
                jobs[j] = jobs[j + 1]; // Desplazar trabajos
            }
            numJobs--; // Reducir número de trabajos
            // No incrementar `i` porque ya hemos desplazado
        } else {
            i++; // Incrementar solo si no se eliminó el trabajo
        }
    }
    
}

//funcion fg
void fg_comando(int job) {
	// Si no le pasamos un proceso, cogemos el ultimo último mandato en background introducido
    if(job==0){
        job = numJobs;
    }
    // Cogemos el pid del ultimo mandato introducido en el jobs
	pid_t pid = jobs[job - 1].pid;
    // Enviamos una señal para reanudar el proceso
	if (kill(pid, SIGCONT) == -1) {
        fprintf(stderr, "Error al reanudar el proceso: %s\n", strerror(errno));
        return;
    }
    // Esperamos a que termine el proceso
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

// Redireccion de archivos, le pasamos el nombre del archivo y el tipo de redireccion
void redirigir_archivo(char *archivo, int tipo) {
	//Descriptor de fichero para interactuar con el archivo
	//En cuanto a los permisos: 
	//En caso de lectura, se abre el archivo en modo lectura.
	//En caso de escritura: se abrira el archivo en modo escritura, se creara si no existe, se vacia si ya existe.
	//0644 es el valor de los permisos que se asignaran al archivo: lectura y escritura para el dueño, lectura para el grupo>
    int fd;
    // El STDIN_FILENO corresponde a tipo 0, STDOUT_FILENO a tipo 1 y STDERR_FILENO a tipo 2
    if (tipo == STDIN_FILENO) {
        fd = open(archivo, O_RDONLY);
    } else if (tipo == STDOUT_FILENO || tipo == STDERR_FILENO) {
        fd = open(archivo, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    }
	
    //Si no se puede abrir el archivo, se imprime el error, usaremos una expresion booleana para identificar el tipo de accion
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

//Funcion para ejecutar comandos, le pasamos la estructura line que contiene los comandos
void ejecutar_comandos(tline *line) {
    int status; // Estado de salida del proceso hijo
	int bg = line->background; // Variable para saber si el proceso se ejecuta en segundo plano

    // Si solo se le pasa 1 comando
    if (line->ncommands == 1) {
        // Crear un proceso hijo con pid 0
        pid_t pid = fork();
        // Si el pid es 0, estamos en el proceso hijo
        if (pid == 0) {
            // Restauramos el manejo por defecto de las señales en el hijo
            signal(SIGINT, SIG_DFL);  // Permitir que SIGINT interrumpa el proceso
            signal(SIGQUIT, SIG_DFL); // Permitir que SIGQUIT interrumpa el proceso
            // Redirección de entrada, salida y error si están especificados
            if (line->redirect_input) redirigir_archivo(line->redirect_input, STDIN_FILENO);
            if (line->redirect_output) redirigir_archivo(line->redirect_output, STDOUT_FILENO);
            if (line->redirect_error) redirigir_archivo(line->redirect_error, STDERR_FILENO);
            // Si no se ha especificado ningún comando, se imprime un error y se sale
            if (line->commands[0].argv[0] == NULL) {
                fprintf(stderr, "Error: No se proporcionó ningún comando para ejecutar.\n");
                exit(1);
            }
    	    // Ejecutar el comando buscandolo en el PATH, si no se encuentra se imprime un error
            if (execvp(line->commands[0].argv[0], line->commands[0].argv) == -1) {
                fprintf(stderr, "Error al ejecutar el comando '%s': %s\n", 
                        line->commands[0].argv[0], strerror(errno));
                exit(1);
            }
        // En caso de que no sea un proceso hijo sino que sea el padre
        } else {
			if (bg) {
                // Construir el nombre completo del comando con sus argumentos
                char fullCommand[1024] = ""; // Un buffer para almacenar el nombre completo
                for (int i = 0; i < line->commands[0].argc; i++) { // Recorremos las argumentos del comando
                    strcat(fullCommand, line->commands[0].argv[i]); //Concatenamos las argumentos del comando
                    if (i < line->commands[0].argc - 1) {
                            strcat(fullCommand, " "); // Añadir un espacio entre argumentos
                    }
                }

				jobs[numJobs].pid = pid;
                // El strdup coge el commando y lo copia en un nuevo espacio de memoria
				jobs[numJobs].nombre = strdup(fullCommand);
				jobs[numJobs].estado = 1;
				numJobs++;
                printf("Proceso en segundo plano con PID %d\n", pid);
			} 
            // Si es un proceso en primer plano esperamos a que termine
            else {
                //Esperamos a que termine el proceso hijo, si hay algun error lo imprimimos
				if(waitpid(pid, &status, 0)==-1){
					fprintf(stderr, "Error al esperar al proceso: %s\n", strerror(errno));
				}
                // Si el comando no termina correctamente o la señal no se envia correctamente, se imprime un error
				else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
					printf("El comando no se ejecutó correctamente\n");
			}
        }
    } 
    // Si hay mas de un comando separado por pipes
    else {
        // Calculamos la cantidad de pipes que es el numero de comandos menos 1
        int num_pipes = line->ncommands - 1;
        // Creamos una matriz de pipes juntando cada 2 comandos adyacentes
        int fd[num_pipes][2];
        for (int i = 0; i < num_pipes; i++) pipe(fd[i]);
        // Iteramos sobre cada comando
        for (int i = 0; i < line->ncommands; i++) {
            // Crear un proceso hijo con pid 0
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);  // Permitir que SIGINT interrumpa el proceso
                signal(SIGQUIT, SIG_DFL); // Permitir que SIGQUIT interrumpa el proceso
                //Si es el primer comando y hay un archivo para redirigir la entrada, se redirige
                if (i == 0 && line->redirect_input) redirigir_archivo(line->redirect_input, STDIN_FILENO);
                // Si no es el primer comando se redirige la entrada del pipe anterior
                if (i > 0) dup2(fd[i - 1][READ_END], STDIN_FILENO);
                // Si es el ultimo comando y hay un archivo para redirigir la salida o el error, se redirige
                if (i == line->ncommands - 1 && line->redirect_output) redirigir_archivo(line->redirect_output, STDOUT_FILENO);
				if (i == line->ncommands - 1 && line->redirect_error) redirigir_archivo(line->redirect_error, STDERR_FILENO); 
                // Si no es el ultimo comando se redirige la salida del pipe posterior
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
        // Si no se ejecuta en segundo plano esperamos a que todos los comandos terminen
        for (int i = 0; i < line->ncommands; i++) wait(&status);
    }
}

int main (void)
{
    // Usamos array para leer el comando 
	char buf[1024];
    // Definimos la estructura de la linea de comandos
	tline * line;
    // Configuramos las señales, para que no se nos cierre la minishell al mandar cualquier señal de control
	configurar_signal_shell();
    // Imprimir el nombre de la shell
	printf("msh> ");
    // Leemos la linea de comandos
	while(fgets(buf, 1024, stdin)){
		// Si la entrada está vacía (es decir, solo se presionó Enter), ignorarla
        if (buf[0] == '\n') {
            printf("msh> ");  // Solo volver a mostrar el prompt
            continue;
        }
        // Tokenizamos la linea de comandos para almacenarla en line
		line = tokenize(buf);
        // Si la linea de comandos da NULL porque no le pasamos nada sigue a la siguiente iteracion
		if (line==NULL) {
			continue;
		}
        // Si el comando es exit, salimos de la minishell
        if(strcmp(line->commands[0].argv[0],"exit")==0){
            exit(0);
        }
        // Si el comando es cd, ejecutamos la funcion cd_comando
		if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "cd") == 0) {
			cd_comando(line->commands[0].argv[1]);
        } 
        // Si el comando es jobs, ejecutamos la funcion jobs
        else if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "jobs") == 0) {
			jobs_comando();
		} 
        //Si el comado es fg se ejecuta el fg_comando, en caso de que no le pasemos ningun argumento pasamos 0
        else if(line->ncommands == 1 && strcmp(line->commands[0].argv[0], "fg") == 0) {
            if(line->commands[0].argc > 1){
		    fg_comando(atoi(line->commands[0].argv[1]));
	        }
            else{
                fg_comando(0);
            }
        } 
        // Si no es ninguno de los anteriores ejecuta el comando normalmente usando el PATH
        else {
			ejecutar_comandos(line);
		}
		printf("msh> ");	
	}
	return 0;
}
