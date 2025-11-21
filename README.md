CLIENTE FTP – INSTRUCCIONES DE USO

Descripción general
Este programa implementa un cliente FTP compatible con el servidor VSFTPD o cualquier servidor que implemente el estándar del protocolo FTP descrito en RFC 959.
El cliente se comunica mediante una conexión de control y abre conexiones de datos en modo PASV o PORT según el comando ingresado.
El programa soporta comandos básicos como USER, PASS, LIST, RETR, STOR, PASV, PORT, además de versiones extendidas como GETC, PORTGET y PORTPUT.


Uso los comandos principales
USER <usuario>
Envía el usuario al servidor FTP.

PASS <contraseña>
Envía la contraseña asociada al usuario. Es necesario para autenticación.

PASV
Coloca al servidor en modo pasivo. La respuesta incluye la IP y puerto para que el cliente abra la conexión de datos. Este modo se usa para LIST, RETR y STOR por defecto.

LIST
Lista los archivos del directorio actual del servidor. Se requiere haber llamado PASV antes para obtener la conexión de datos.

GETC <archivo>
Descarga un archivo usando PASV internamente. El cliente abre la conexión de datos y escribe el archivo localmente.

STOR <archivo>
Envía un archivo desde el cliente hacia el servidor usando PASV.

PORT
Activa el modo PORT. El cliente abre un socket local en modo escucha y envía al servidor la dirección donde el servidor debe conectarse.

PORTGET <archivo>
Descarga un archivo usando modo PORT. El cliente escucha conexiones entrantes y recibe los datos.

PORTPUT <archivo>
Sube un archivo usando modo PORT. El servidor se conecta al socket de datos abierto por el cliente.

Cómo ejecutar el cliente FTP

Compilar utilizando gcc dentro de WSL:
gcc -o clientftp SimbanaF-clienteFTP.c connectTCP.c connectsock.c errexit.c

O ejecutar el archivo Makefile con 
make

Ejecutar el cliente:
./clientftp <hostname> <puerto>
Ejemplo:
./clientftp 127.0.0.1 21

Una vez dentro del cliente, ingresar comandos como:
USER anonymous
PASS test
PASV
LIST
GETC archivo.txt
STOR archivo.txt
PORT
PORTGET archivo.txt
