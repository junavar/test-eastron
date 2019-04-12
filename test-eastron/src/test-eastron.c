/*
 ============================================================================
 Name        : test-eastron.c
 Author      : juan
 Version     :
 Copyright   : Your copyright notice
 Description : Prueba de lectura periodica de medidor electrico eastron (SDM230)
 ============================================================================
 */

#define VERSION "0.36"


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>



/*
 * pasa a flotante los 2 registros de 16 bits que devuelve libmodbus.
 */
float pasar_4_bytes_a_float_2(unsigned char * buffer) {
	unsigned char tmp[4];

	tmp[3] = buffer[1];
	tmp[2] = buffer[0];
	tmp[1] = buffer[3];
	tmp[0] = buffer[2];

	//return *(float *) (tmp);   da el warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]
	//return *(float *) (unsigned long *) (tmp);da el warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]

	float *pvalor;
	pvalor=(float *)tmp;
	return *pvalor;
}

char * get_iso_time(){
	time_t tiempo;
	tiempo=time(0);
	static char buf[sizeof("2019-01-01T09:00:00+00:00")];
	strftime(buf, sizeof buf, "%FT%T%z", localtime(&tiempo));
	return (&buf[0]);
}

/*
 * Ajusta un temporizador para cada segundo usando File Descriptors
 */
int init_espera_siguiente_segundo(){

		int fd_timer_segundo; //
		fd_timer_segundo = timerfd_create(CLOCK_REALTIME, 0);
		if(fd_timer_segundo==-1) {
			return -1;
		}
		struct timeval tiempo_alineado; //
		struct itimerspec timer;

		/* obtiene el siguiente segundo en tiempo absoluto*/
		/* obtiene el tiempo actual e incremeta el segundo y pone a cero los microsegundos */
		gettimeofday(&tiempo_alineado, NULL);
		tiempo_alineado.tv_sec+=1;
		tiempo_alineado.tv_usec=0;

		/* ajusta salto de temporizador con el siguiente segunto en tiempo absoluto*/
		timer.it_value.tv_sec=tiempo_alineado.tv_sec;
		timer.it_value.tv_nsec=0;

		/* ajusta salto periodico a 1s */
		timer.it_interval.tv_sec=1;
		timer.it_interval.tv_nsec=000000000;

		/* aplica los ajustes */
		if (timerfd_settime(fd_timer_segundo, TFD_TIMER_ABSTIME, &timer, NULL) == -1){
			close(fd_timer_segundo);
			return -1;
		}
		return fd_timer_segundo;
};

/*
 * Se bloquea hasta el siguiente segundo
 * Devuelve el numero de faltas de puntualidad
 */
int espera_siguiente_segundo(int fd_timer_segundo){
	uint64_t s;
	uint64_t num_expiraciones;

	s = read(fd_timer_segundo, &num_expiraciones, sizeof(num_expiraciones)); /* esta funcion debe leer obligatoriamente 8 bytes del fichero temporizador por eso se declara como uint64_t*/
	if (s != sizeof(num_expiraciones)) {
		return -1;
	}
	return (int)(num_expiraciones - 1);
}



int main(void) {

	fprintf(stderr, "test-eastron %s\n", VERSION);
	modbus_t *ctx;
	ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
	if (!ctx) {
		fprintf(stderr, "Failed to create the context: %s\n",
				modbus_strerror(errno));
		exit(1);
	}

	if (modbus_connect(ctx) == -1) {
		fprintf(stderr, "Unable to connect: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		exit(1);
	}

	//Set the Modbus address of the remote slave (to 3)
	modbus_set_slave(ctx, 1);

	uint16_t reg[128]; // will store read registers values

	//Read 32 holding registers starting from address 0
	int reg_solicitados = 32;
	int num;
	int espera=350000; //para 9600B
	//int espera=10000 //para 2400B
	int fd_timer_segundo;
	fd_timer_segundo=init_espera_siguiente_segundo();
	if(fd_timer_segundo<0){
		fprintf(stderr, "Error en temporizador errno:%d", errno);
	}

	int num_faltas=0;
	int faltas_acumuladas=0;
	int lecturas=0; // numero de lecturas realizadas
	int err=0; //errores
	float terr=0.0; // tasa error

	for (;;) {

		num_faltas=espera_siguiente_segundo(fd_timer_segundo);
		faltas_acumuladas+=num_faltas;


		char * tiempo;
		tiempo=get_iso_time();
		printf("%s ", tiempo);
		modbus_flush(ctx); // tira los datos que se hubiesen recibido y no leidos antes de la solicitud
		num = modbus_read_input_registers(ctx, 0x00, reg_solicitados, reg);
		if (num != reg_solicitados) { // number of read registers is not the one expected
			fprintf(stderr, "%s Failed to read: %s\n", get_iso_time(), modbus_strerror(errno));
			err++;
		}
		lecturas++;

		printf("%03.0fV "   , pasar_4_bytes_a_float_2((unsigned char *) &reg[0]));
		printf("%02.2fA ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x06]));
		printf("%04.0fW ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x0C]));
		printf("%02.0fva ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x18]));
		printf("fp:%0.2f ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x1E]));

		usleep(espera);

		modbus_flush(ctx); // tira los datos que se hubiesen recibido y no leidos antes de la solicitud
		//Read 2 holding registers starting from address 0x46 (frecuencia)
		num = modbus_read_input_registers(ctx, 0x46, reg_solicitados, reg);
		if (num != reg_solicitados) { // number of read registers is not the one expected
			fprintf(stderr, "%s Failed to read: %s\n", get_iso_time(), modbus_strerror(errno));
			err++;
		}
		lecturas++;

		printf("%02.2fHz ",     pasar_4_bytes_a_float_2((unsigned char *) &reg[0x46-0x46]));
		printf("Imp:%06.2fkWh ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x48-0x46]));
		printf("Exp:%06.2fkWh ", pasar_4_bytes_a_float_2((unsigned char *) &reg[0x4A-0x46]));

		terr=((float)err/(float)lecturas)*100;
		printf("lecturas:%d err:%d tasa_error:%2.3f%% faltas_acum:%d ", lecturas, err, terr, faltas_acumuladas );


		fflush(stdout);


		printf("\r");

	}

	modbus_close(ctx);
	modbus_free(ctx);

	return EXIT_SUCCESS;

}
