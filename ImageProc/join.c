
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <omp.h>

#ifdef NEON
#include <arm_neon.h>
#endif

#ifdef TIMMING
#include <sys/time.h>
#endif

#ifndef NO_DEG_LOOKUPTABLE
#include "lookupTable.h"
#endif

#define RAD_2_DEG 57.29577951308232

static float * G_copiaImagen;
static float * G_bufferDeTrabajo;
static float * G_filtroRuido1D;
static float ** G_copiaImagenM;
static float ** G_bufferDeTrabajoM;
static float ** G_filtroRuido2D; 
static float ** G_filtroGradienteX;
static float ** G_filtroGradienteY;
static unsigned int G_width;
static unsigned int G_height;
static unsigned int G_tamFiltro;
static unsigned int G_size;


#ifndef THREADS
#ifdef RASPI2
#define THREADS 4
#else
#define THREADS 2
#endif
#endif
static void getM_B(float smax, float smin, float * m, float *b)
{
	*m = 255. / (smax - smin);
	*b = - (smin * *m);
}
		
static void escalar_Float_Uint8(float * ygradiente, uint8_t * ygescalado, unsigned int size)
{
	float * sptr,
	* maxsptr = ygradiente + size;

	uint8_t * dptr,
	* maxdptr = ygescalado + size;

	float vmax = ygradiente[0], vmin = ygradiente[0];

	for(sptr = ygradiente; sptr < maxsptr; sptr++)
	{
		if(*sptr <= vmin) vmin = *sptr;
		if(*sptr >= vmax) vmax = *sptr;
	}

	float M, B;

	getM_B(vmax, vmin, &M, &B);
#ifdef DEBUG
	fprintf(stderr, "Maximo: %f , Minimo: %f\n", vmax, vmin);
#endif

	for(sptr = ygradiente, dptr = ygescalado; sptr < maxsptr; sptr++, dptr++)
	{
		*dptr = round(*sptr * M + B);	
	}

}
static void escalar_Uint8_Uint8(uint8_t * ygradiente, uint8_t * ygescalado, unsigned int size)
{
	uint8_t * sptr,
	* maxsptr = ygradiente + size;

	uint8_t * dptr,
	* maxdptr = ygescalado + size;

	float vmax = -9999, vmin = 9999;

	for(sptr = ygradiente; sptr < maxsptr; sptr++)
	{
		if(*sptr <= vmin) vmin = *sptr;
		if(*sptr >= vmax) vmax = *sptr;
	}

	float M, B;

	getM_B(vmax, vmin, &M, &B);
#ifdef DEBUG
	fprintf(stderr, "Maximo: %f , Minimo: %f\n", vmax, vmin);
#endif

	for(sptr = ygradiente, dptr = ygescalado; sptr < maxsptr; sptr++, dptr++)
	{
		*dptr = (uint8_t)round(*sptr * M + B);	
	}

}



static void copiarArray_Uint8_Float(float* dst, uint8_t* src, unsigned int size)
{
	uint8_t * sptr;
	float *  dptr;

	uint8_t * maxsptr = src + size;
	for(sptr = src, dptr = dst; sptr < maxsptr; sptr++, dptr++)
	{
		*dptr = (float) *sptr;
	}
}

static char *
pgmName(char *path)
{
	char *name = strrchr(path, '/');
	return name ? name + 1 : path;
}


static void
usage(char *pgmname)
{
	fprintf(stderr, "%s [opciones] < img.ppm\n"
			"Descripcion:\n"
			"Recibe por la entrada estandar una imagen RGB en formato ppm y escribe en ficheros pgm (XX-etapa.pgm) el resultado del algoritmo Canny primero, y luego Hough, en cada etapa. Tambien genera la version original de 'img.ppm' en ppm (rgb) y ppm (escala de grises).\n"
			"Opciones:\n"
			"\t-l               - Tamano del filtro gausiano (>=3 e impar).\n"
			"\t-s               - Sigma del filtro gausiano.\n"
			"\t-U               - Umbral superior para deteccion de borde (0 < x < 255).\n"
			"\t-L               - Umbral inferior para deteccion de borde (0 < x < 255).\n"
			"\t-a               - Numbero de angulos de la transformada de hough.\n"
			"\t-e               - Enviar por la salida estandar la imagen pgm de acumuladores del espacio de Hough.\n"
			"\t-d               - Directorio de salida.\n", pgmname);
	exit(-1);
}

static int
optGetIntError(char *optarg, int *val, int min)
{
	int x = atoi(optarg);
	if (x <= min)
		return 1;
	*val = x;
	return 0;
}

static float
optGetFloatError(char *optarg, float *val, float min)
{
	float x = atof(optarg);
	if (x <= min)
		return 1;
	*val = x;
	return 0;
}


static int
getOptions(int argc, char *argv[], int * filterSize, float *sigma, unsigned int *uth, unsigned int * lth, int *re,
int *nangulos, char ** outdir)
{
	int opt;
	int error = 0;
	*re = 0;
	*filterSize = -1; *sigma = -1;
	*nangulos = -1;
	*re = 0;
	*outdir = NULL;
	while ((opt = getopt(argc, argv, "l:s:U:L:a:ed:")) != -1) {
		switch (opt) {
		case 'l':
			error += optGetIntError(optarg, filterSize, 0);
			break;
		case 's':
			error += optGetFloatError(optarg, sigma, 0);
			break;
		case 'U':
			error += optGetIntError(optarg, uth, -1);
			break;
		case 'L':
			error += optGetIntError(optarg, lth, -1);
			break;
		case 'a':
			error += optGetIntError(optarg, nangulos, 0);
			break;
		case 'e':
			*re = 1;
			break;
		case 'd':
			*outdir = optarg;
			break;
		default: /* '?' */
			usage(argv[0]);
		}
	}

	#ifdef FS_3
		*filterSize = 3;
	#elif FS_5
		*filterSize = 5;
	#elif FS_7
		*filterSize = 7;
	#endif
	if (error) {
		usage(pgmName(argv[0]));
	}
	if (optind < argc) {
		fprintf(stderr, "Unexpected argument(s) after options\n");
		usage(pgmName(argv[0]));
	}
	if(*filterSize == -1 || *sigma == -1)
	{
		fprintf(stderr, "Missing required options\n");
		usage(pgmName(argv[0]));
	}
	if(*filterSize % 2 == 0 || *filterSize < 3)
	{
		fprintf(stderr, "El filtro no es correcto\n");
		usage(pgmName(argv[0]));
	}
	if(*lth > 255 || *uth > 255 || *lth < 0 || *uth < 0)
	{
		fprintf(stderr, "El umbral no es correcto\n");
		usage(pgmName(argv[0]));
	}
	if(*nangulos < 0) 
	{
		fprintf(stderr, "Necesario especificar el numero de angulos\n");
		usage(pgmName(argv[0]));
	}
	if(*outdir == NULL)
	{
		*outdir = malloc(strlen(".")+1);
		strcpy(*outdir, ".");
	}

}

static int
readint(int *x, int *next, FILE *f)
{
	int error = -1;
	int v = 0;
	int c;
	if (isdigit(c = getc(f))) {
		v = c - '0';
		while (isdigit(c = getc(f))) {
			v = v * 10 + (c - '0');
		}
		*x = v;
		error = 0;
	}
	*next = c;
	return error;
}

static int
readwh(int *w, int *h, FILE *f)
{
	int next;
	if (readint(w, &next, f) || next != ' ')
		return -1;
	if (readint(h, &next, f) || next != '\n')
		return -1;
	return 0;
}

static int
readmax(int *max, FILE *f)
{
	int next;
	return (readint(max, &next, f) || next != '\n') ? -1 : 0;
}

static int
read_ppmHeader(int *w, int *h)
{
	if (getc(stdin) != 'P')
		return -1;
	int c = getc(stdin);
	if (c != '6')
		return -1;
	if (getc(stdin) != '\n')
		return -1;
	if (readwh(w, h, stdin))
		return -1;
	int max;
	if (readmax(&max, stdin) || max != 255)
		return -1;
	return 0;
}


int rgb2gs(unsigned char * rgb, unsigned char * gs, unsigned int width, unsigned int height)
{
	//Y <- 0.299R + 0.587G + 0.114B

	unsigned int pixelLength = width * height;	 

	unsigned char * maxGs = gs + pixelLength;


	while(gs < maxGs)
	{
		*gs++ = 0.299**rgb++ +0.587**rgb++ +0.114**rgb++;
	}
	return 1;
}

void showFilter(FILE * s, float * filter, int size)
{
	fprintf(s, "[ ");
	int i;
	for(i=0; i<size; i++)
	{
		fprintf(s, " %.4f, ", filter[i]);
	}
	fprintf(s, " ]\n");

}
void showFilterM(FILE * s, float ** filter, int height, int width)
{
	fprintf(s, "[ ");
	int f,c;

	for(f=0; f < height; f++)
	{
		fprintf(s, "[ ");
		for(c=0; c < width; c++)
		{
			fprintf(s, " %.4f, ", filter[f][c]);
		}

		fprintf(s, " ]\n");
	}
	fprintf(s, " ]\n");

}
float gaussianZeroMean(float x, float sigma)
{
	float res = pow(M_E, (-1./2)*pow(x/sigma,2));

	return res;
}

float gaussianZeroMean2D(float x, float y, float sigma)
{
	float res = pow(M_E, (-1./2)*(x*x+y*y)/(sigma*sigma));

	return res;
}

void getGaussian2DFilter(float ** filtro, unsigned int tam, float sigma)
{
	int li = -(tam >> 1);
	int ls = li*(-1);
	float v, suma=0;
	int x, y;
#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "[\n");
#endif

	for(x=li; x<=ls; x++)
	{
#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "[");
#endif
		for(y=li; y<=ls; y++)
		{
			v = gaussianZeroMean2D(x,y,sigma);
			filtro[x+ls][y+ls] = v;
#ifdef SHOW_GAUSSIAN_FILTER
		fprintf(stderr, "%.4f , ",v);
#endif

			suma += v;
		}
#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "]\n");
#endif

	}
#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "] * 1/%.4f\n", suma);
#endif


	for(x=li; x<=ls; x++)
	{
		for(y=li; y<=ls; y++)
		{
			filtro[x+ls][y+ls] = filtro[x+ls][y+ls] / suma;
		}
	}


}



void getGaussianFilter(float * filtro, unsigned int tam, float sigma)
{
	int offset = tam >> 1;
	int idx;
	float * ptr;
	float sum = 0;

#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "[");
#endif
	for(idx = -offset, ptr = filtro; idx <= offset; idx++, ptr++)
	{
		*ptr = gaussianZeroMean(idx, sigma);
#ifdef SHOW_GAUSSIAN_FILTER
		fprintf(stderr, "%.4f , ",*ptr);
#endif
		sum += *ptr;
	}
	for(idx = 0; idx < tam; idx++)
	{
		filtro[idx] /= sum;
	}

#ifdef SHOW_GAUSSIAN_FILTER
	fprintf(stderr, "]*1/%.4f\n", sum);
#endif
}
static void getMatrixFromArray_Float_buffer(float* vec, unsigned int width, unsigned int height, unsigned int size, float** mat)
{
	float * sptr;
	int pos;
	sptr = vec;
	for(pos = 0; pos < height; pos += 1)
	{
		mat[pos] = sptr;
		sptr += width;
	}
}

static void getMatrixFromArray_Uint8_buffer(uint8_t* vec, unsigned int width, unsigned int height, unsigned int size, uint8_t ** mat)
{
	uint8_t * sptr;
	int pos;
	sptr = vec;
	for(pos = 0; pos < height; pos += 1)
	{
		mat[pos] = sptr;
		sptr += width;
	}
}


static float ** getMatrixFromArray_Float(float* vec, unsigned int width, unsigned int height, unsigned int size)
{

	float ** mat = (float**) malloc(height * sizeof(float*));

	float * sptr;
	int pos;
	sptr = vec;
	for(pos = 0; pos < height; pos += 1)
	{
		mat[pos] = sptr;
		sptr += width;
	}
	return mat;
}

static uint8_t ** getMatrixFromArray_Uint8(uint8_t* vec, unsigned int width, unsigned int height, unsigned int size)
{

	uint8_t ** mat = (uint8_t**) malloc(height * sizeof(uint8_t*));

	uint8_t * sptr;
	int pos;
	sptr = vec;
	for(pos = 0; pos < height; pos += 1)
	{
		mat[pos] = sptr;
		sptr += width;
	}
	return mat;
}



static void obtenerModuloGradiente(float * xg, float * yg, float * mg, unsigned int width, unsigned int height)
{
	unsigned int length = width * height;

	int i;	
	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime)
	for(i = 0 ; i < length; i++)
	{
		float x = xg[i];
		float y = yg[i];
		mg[i]=sqrt(x*x+y*y);
	}
	
	
}

static void obtenerDireccionGradiente(float * xg, float * yg, float * mg, unsigned int width, unsigned int height)
{
	unsigned int length = width * height;
	
	float * xptr, *yptr, *mptr, *maxxptr;

	for(xptr = xg, yptr = yg, mptr = mg, maxxptr = xptr + length; xptr < maxxptr; mptr++, xptr++, yptr++)
	{
		*mptr = atan(*yptr / *xptr);
	}
	
}
static void freeAll()
{
	free(G_copiaImagen);
	free(G_bufferDeTrabajo);
	free(G_filtroRuido1D);
	free(G_copiaImagenM);
	free(G_bufferDeTrabajoM);
	free(G_filtroRuido2D);
	free(G_filtroGradienteX);
	free(G_filtroGradienteY);
}
static void init(uint8_t * img, unsigned int width, unsigned int height, float sigma, unsigned int tamFiltro)
{
	unsigned int size = width * height;

	G_size = size;
	G_height = height;
	G_width = width;
	G_copiaImagen = (float * ) malloc(size * sizeof(float));
	G_bufferDeTrabajo = (float * ) malloc(size * sizeof(float));
	G_tamFiltro = tamFiltro;
	int f;

	float * _filtroGradienteX, * _filtroGradienteY, *_filtroRuido2D, * auxptr;
	
	G_filtroRuido2D = (float**) malloc(tamFiltro*sizeof(float*));
	_filtroRuido2D = (float*) malloc(tamFiltro*tamFiltro * sizeof(float)+1);

	for(f = 0, auxptr = _filtroRuido2D; f < tamFiltro; f++, auxptr += tamFiltro)
	{
		G_filtroRuido2D[f] = auxptr;
	}

	G_filtroRuido1D = (float*) malloc(tamFiltro * sizeof(float)+1);
	
	getGaussianFilter(G_filtroRuido1D, tamFiltro, sigma);
	getGaussian2DFilter(G_filtroRuido2D, tamFiltro, sigma);

	showFilter(stderr, G_filtroRuido1D, tamFiltro);
	showFilterM(stderr, G_filtroRuido2D, tamFiltro, tamFiltro);

	int gsize = 3;

	float ** Gy = (float **) malloc(gsize * sizeof(float*));
	float * Gyc = (float *) malloc(gsize * gsize *  sizeof(float));
	float * gptr;
	for(f = 0, gptr = Gyc; f < gsize; f++, gptr += gsize)
	{
		Gy[f] = gptr;
	}

	Gy[0][0] = -1; Gy[0][1] = -2; Gy[0][2] = -1;
	Gy[1][0] =  0; Gy[1][1] =  0; Gy[1][2] =  0;
	Gy[2][0] =  1; Gy[2][1] =  2; Gy[2][2] =  1;

	float ** Gx = (float **) malloc(gsize * sizeof(float*));
	float * Gxc = (float *) malloc(gsize * gsize *  sizeof(float));
	for(f = 0, gptr = Gxc; f < gsize; f++, gptr += gsize)
	{
		Gx[f] = gptr;
	}

	Gx[0][0] = -1; Gx[0][1] = 0; Gx[0][2] = 1;
	Gx[1][0] = -2; Gx[1][1] = 0; Gx[1][2] = 2;
	Gx[2][0] = -1; Gx[2][1] = 0; Gx[2][2] = 1;

	G_filtroGradienteX = Gx;
	G_filtroGradienteY = Gy;

	G_copiaImagenM = getMatrixFromArray_Float(G_copiaImagen, width, height, size);
	G_bufferDeTrabajoM = getMatrixFromArray_Float(G_bufferDeTrabajo, width, height, size);

}
static void showError()
{
	fprintf(stderr, "Ha ocurrido algun error: %s\n", strerror(errno));
}


#ifdef DEBUG
static unsigned int escalarDireccionDiscreta(uint8_t dir)
{
	switch(dir)
	{
		case 0:
			return 0;
		case 45:
			return 85;
		case 90:
			return 170;
		case 135:
			return 255;
		default:
			return 255;
	}

}
#endif


static void nonMaximum(float ** mgM, uint8_t ** dgM, float ** mgtM, unsigned int width, unsigned int height)
{
	unsigned int size = width * height;

	//float * mg = mgM[0];
	//float * mgthin = mgtM[0];

	//memcpy(mgthin, mg, size*sizeof(float));

	unsigned int foffset = 1;

	int f, c;
	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime) private(c)
	for(f=foffset; f < height-foffset; f++)
	{
		for(c=foffset; c < width-foffset; c++)
		{
			int mf1,mf2,mc1,mc2;
			//Miramos la direccion del gradiente

			int caso = -1;
			switch(dgM[f][c])
			{
				case 0:
					caso = 0;
					mf1 = f;
					mc1 = c - 1;
					mf2 = f;
					mc2 = c + 1;
					break;
				case 45:	
					caso = 45;
					mf1 = f - 1;
					mc1 = c + 1;
					mf2 = f + 1;
					mc2 = c - 1;
					break;
				case 90:
					caso = 90;
					mf1 = f - 1;
					mc1 = c;
					mf2 = f + 1;
					mc2 = c;
					break;
				case 135:
					caso = 135;
					mf1 = f - 1;
					mc1 = c - 1;
					mf2 = f + 1;
					mc2 = c + 1;
					break;
				default:
					fprintf(stderr, "ERROR: angulo incorrecto en nonMaximum\n");
					exit(1);

			}

			//fprintf(stderr, "caso %d\n", caso);
			float v = mgM[f][c];

			if(v < mgM[mf1][mc1] || v < mgM[mf2][mc2])
			{
				//fprintf(stderr, "%d-%d , %d-%d, %d-%d\n", mf1,mc1,f,c,mf2,mc2);
				mgtM[f][c] = 0;
			}
			else
				mgtM[f][c] = v;
		
		}
	}

}

static void hysteresis(uint8_t ** srcM, uint8_t ** dstM, uint8_t alto, uint8_t bajo, unsigned int width, unsigned int height)
{

	unsigned int size = width * height;

	uint8_t vmax = 255, vmin = 0;
	int f, c;
	int maxHeight = height-1;
	int maxWidth = width-1;

	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime) private(c)
	for(f = 1; f < maxHeight; f++)
	{
		for(c = 1; c < maxWidth; c++)
		{
			uint8_t v = srcM[f][c];
			if(v > alto)
				dstM[f][c] = vmax;
			else if (v > bajo)
			{
				int fb = f-1,fa = f+1, cb = c-1, ca =c+1;
				//miramos los vecinos
				if(srcM[fb][cb] > alto 
				|| srcM[fb][c] > alto
				|| srcM[fb][ca] > alto
				|| srcM[f][cb] > alto
				|| srcM[f][ca] > alto
				|| srcM[fa][cb] > alto
				|| srcM[fa][c] > alto
				|| srcM[fa][ca] > alto)
					dstM[f][c] = vmax;
				else
					dstM[f][c] = vmin;
			}
			else
				dstM[f][c] = vmin;
		}
	}
	
}

static void saveImage(int fd, uint8_t * header, unsigned int hlength, uint8_t * content, unsigned int clength)
{
	int writen = write(fd, header, hlength);
	writen += write(fd, content, clength);
#ifdef DEBUG
	fprintf(stderr, "Escritos: %d\n", writen);
#endif
}

static void saveImage_Float(int fd, uint8_t * header, unsigned int hlength, float * content, unsigned int clength)
{
	int writen = write(fd, header, hlength);
	uint8_t * scaled = (uint8_t*) malloc(clength);

	escalar_Float_Uint8(content, scaled, clength);

	writen += write(fd, scaled, clength);
#ifdef DEBUG
	fprintf(stderr, "Escritos: %d\n", writen);
#endif
}

void houghAcc(int x, int y, unsigned int ** acc, float * sinTable, float * cosTable, unsigned int width, unsigned int height, unsigned int nangulos, unsigned int rhoOffset, int vtrue)
{
	float rho;
	int thetaIndex;

	int x0=x,y0=y;
	x -= width;
	y -= height;

	for(thetaIndex = 0; thetaIndex < nangulos; thetaIndex++)
	{
		float rho0 = x * cosTable[thetaIndex] + y * sinTable[thetaIndex];
		int rho = ceil(rho0)+rhoOffset;
		acc[rho][thetaIndex] += 1;

	}


	
}


static void escalar_Int_Uint8(int * src, uint8_t * dst, unsigned int length)
{
	int * sptr,
	* maxsptr = src + length;

	uint8_t * dptr,
	* maxdptr = dst + length;

	int vmax = -9999, vmin = 9999;

	for(sptr = src; sptr < maxsptr; sptr++)
	{
		if(*sptr <= vmin) vmin = *sptr;
		if(*sptr >= vmax) vmax = *sptr;
	}

	float M, B;

	getM_B(vmax, vmin, &M, &B);

#ifdef DEBUG
	fprintf(stderr, "Maximo: %d , Minimo: %d\n", vmax, vmin);
#endif

	for(sptr = src, dptr = dst; sptr < maxsptr; sptr++, dptr++)
	{
		*dptr = (uint8_t)round(*sptr * M + B);	
	}

}


static void invertirValores(uint8_t * src, unsigned int length)
{
	uint8_t * ptr, *mptr = src+length;
	for(ptr = src; ptr < mptr; ptr++)
	{
		*ptr = 255 - *ptr;
	}
}


#ifdef TIMMING
static void mostrarTiempo(const char * info, struct timeval *t0, struct timeval *t1, long long *acc)
{
	long long elapsed = (t1->tv_sec-t0->tv_sec)*1000000LL + t1->tv_usec-t0->tv_usec;

	fprintf(stderr, "%s:\t%lld us\n",info, elapsed);
	if(acc != NULL)
		*acc += elapsed;
	
}
#endif


static void aplicarFiltro_noSeparable_size5(float ** gsFiltradoM)
{
	float ** sM = G_copiaImagenM;
	float ** dM = gsFiltradoM;
	float ** filtro = G_filtroRuido2D;
	unsigned int height = G_height;
	unsigned int width = G_width;
	
	//cambia
	unsigned int foffset = 2;
	//fin-cambia

	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int f;

#ifdef NEON
	float32x4_t ff0,ff1,ff2,ff3,ff4;
	ff0 = vld1q_f32(filtro[0]);
	ff1 = vld1q_f32(filtro[1]);
	ff2 = vld1q_f32(filtro[2]);
	ff3 = vld1q_f32(filtro[3]);
	ff4 = vld1q_f32(filtro[4]);
#endif
	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime)
	for(f=foffset; f < maxHeight; f++)
	{
		int c;
		for(c=foffset; c < maxWidth; c++)
		{

			#ifdef NEON
			int c0=c-2, c1=c-1, c2=c, c3=c+1, c4=c+2,
			f0=f-2, f1=f-1, f2=f, f3=f+1, f4=f+2;
			
			float32x4_t nsum, tmp, resf;
			nsum = vdupq_n_f32(0);

			float * cpixel;
			float * psM;
			cpixel = &dM[f][c];

			psM = &sM[f0][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f1][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f2][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f3][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff3, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f4][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff4, tmp);
			nsum = vaddq_f32(nsum, resf);

			float32x2_t da = vget_low_f32(nsum);
			float32x2_t db = vget_high_f32(nsum);
			da = vadd_f32(da, db);

			*cpixel = vget_lane_f32(da,0);
			*cpixel += vget_lane_f32(da,1);
			*cpixel += sM[f0][c4] * filtro[0][4];
			*cpixel += sM[f1][c4] * filtro[1][4];
			*cpixel += sM[f2][c4] * filtro[2][4];
			*cpixel += sM[f3][c4] * filtro[3][4];
			*cpixel += sM[f4][c4] * filtro[4][4];

			#else


			int c0=c-2, c1=c-1, c2=c, c3=c+1, c4=c+2,
			f0=f-2, f1=f-1, f2=f, f3=f+1, f4=f+2;

			float * cpixel = &dM[f][c];
			
			*cpixel = 0;
			*cpixel += sM[f0][c0] * filtro[0][0];
			*cpixel += sM[f0][c1] * filtro[0][1];
			*cpixel += sM[f0][c2] * filtro[0][2];
			*cpixel += sM[f0][c3] * filtro[0][3];
			*cpixel += sM[f0][c4] * filtro[0][4];

			*cpixel += sM[f1][c0] * filtro[1][0];
			*cpixel += sM[f1][c1] * filtro[1][1];
			*cpixel += sM[f1][c2] * filtro[1][2];
			*cpixel += sM[f1][c3] * filtro[1][3];
			*cpixel += sM[f1][c4] * filtro[1][4];

			*cpixel += sM[f2][c0] * filtro[2][0];
			*cpixel += sM[f2][c1] * filtro[2][1];
			*cpixel += sM[f2][c2] * filtro[2][2];
			*cpixel += sM[f2][c3] * filtro[2][3];
			*cpixel += sM[f2][c4] * filtro[2][4];

			*cpixel += sM[f3][c0] * filtro[3][0];
			*cpixel += sM[f3][c1] * filtro[3][1];
			*cpixel += sM[f3][c2] * filtro[3][2];
			*cpixel += sM[f3][c3] * filtro[3][3];
			*cpixel += sM[f3][c4] * filtro[3][4];

			*cpixel += sM[f4][c0] * filtro[4][0];
			*cpixel += sM[f4][c1] * filtro[4][1];
			*cpixel += sM[f4][c2] * filtro[4][2];
			*cpixel += sM[f4][c3] * filtro[4][3];
			*cpixel += sM[f4][c4] * filtro[4][4];


			#endif
		}
	}


}

static void aplicarFiltro_noSeparable(float ** gsFiltradoM)
{
	unsigned int tfiltro = G_tamFiltro;
	float ** sM = G_copiaImagenM;
	float ** dM = gsFiltradoM;
	float ** filtro = G_filtroRuido2D;
	unsigned int height = G_height;
	unsigned int width = G_width;
	
	
	unsigned int foffset = tfiltro >> 1;
	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int f;

	for(f=foffset; f < maxHeight; f++)
	{
		int c;
		for(c=foffset; c < maxWidth; c++)
		{
			int f2,c2, ff,fc, f2o = f-foffset, c2o = c-foffset;
			float v = 0;


			for(f2 = f2o, ff = 0; ff < tfiltro; f2++, ff++)
			{
				for(c2 = c2o, fc = 0; fc < tfiltro; c2++, fc++)
				{
					v += sM[f2][c2] * filtro[ff][fc];
				}
			}
			dM[f][c] = v;
		}
	}

}
#if !defined(FILTRO_NOCACHE) || !defined(GRAD_NOCACHE)
typedef struct RegionLoc
{
	unsigned int fini,cini,ffin,cfin,idx;
} RegionLoc;


static RegionLoc * getRegionsLoc(unsigned int *nregions, unsigned int rw, unsigned int rh, unsigned int imw, unsigned int imh, unsigned int filterOffset, unsigned int * regPerRow)
{

	unsigned int overlapping = filterOffset*2;
	unsigned int filterSize = overlapping > 0 ? overlapping+1 : 0;
	unsigned int hinc = rw-overlapping;
	unsigned int vinc = rh-overlapping;

	unsigned int lastRegCol = rw-1;
	unsigned int lastRegRow = rh-1;

	unsigned int lastImCol = imw-1;
	unsigned int lastImRow = imh-1;

	unsigned int regionsize = rw*rh;
	unsigned int imsize = imw*imh;
	RegionLoc *regions = (RegionLoc*)malloc(imsize*sizeof(RegionLoc));

	*nregions = 0;
	int fila, maxf, col, maxc;

	maxf = imh/vinc * vinc;
	*regPerRow = imw/hinc;

	maxc = *regPerRow * hinc;

	int firstColLastRegionInRow = maxc;
	int firstRowLastRegionInCol = maxf;


	for(fila = 0; fila < firstRowLastRegionInCol; fila += vinc)
	{
		int filaFin = fila + lastRegRow;
		if(filaFin <= lastImRow)
		{
			for(col = 0; col < firstColLastRegionInRow ; col += hinc)
			{
				int colFin = col + lastRegCol;
				if(colFin <= lastImCol)
				{
					RegionLoc *reg = &regions[*nregions];
					reg->fini = fila;
					reg->ffin = filaFin;
					reg->cini = col;
					reg->cfin = colFin;
		#ifdef PRINT_REGIONS
					fprintf(stderr, "Region %d: (%d,%d; %d,%d)\n",(int) *nregions, (int) reg->fini, (int)reg->cini, (int)reg->ffin, (int)reg->cfin);
		#endif
					reg->idx = *nregions;
					*nregions += 1;
				}
			}
			RegionLoc *reg = &regions[*nregions];
			reg->fini = fila;
			reg->ffin = filaFin;
			reg->cini = col;
			reg->cfin = lastImCol;
	#ifdef PRINT_REGIONS
			fprintf(stderr, "Region %d: (%d,%d; %d,%d)\n",(int) *nregions, (int) reg->fini, (int)reg->cini, (int)reg->ffin, (int)reg->cfin);
	#endif
			reg->idx = *nregions;
			*nregions += 1;
		}
	}
	if(imh - fila >= filterSize)
	{
		int filaFin = lastImRow;
		for(col = 0; col < firstColLastRegionInRow; col += hinc)
		{
			int colFin = col + lastRegCol;
			if(colFin <= lastImCol)
			{
				RegionLoc *reg = &regions[*nregions];
				reg->fini = fila;
				reg->ffin = filaFin;
				reg->cini = col;
				reg->cfin = colFin;

	#ifdef PRINT_REGIONS
				fprintf(stderr, "Region %d: (%d,%d; %d,%d)\n",(int) *nregions, (int) reg->fini, (int)reg->cini, (int)reg->ffin, (int)reg->cfin);
	#endif
				reg->idx = *nregions;
				*nregions += 1;
			}
		}
		RegionLoc *reg = &regions[*nregions];
		reg->fini = fila;
		reg->ffin = filaFin;
		reg->cini = col;
		reg->cfin = lastImCol;

#ifdef PRINT_REGIONS
		fprintf(stderr, "Region %d: (%d,%d; %d,%d)\n",(int) *nregions, (int) reg->fini, (int)reg->cini, (int)reg->ffin, (int)reg->cfin);
#endif
		reg->idx = *nregions;
		*nregions += 1;

	}


	return regions;



}
static void getRegion(float *region , int h, int w, float ** img, RegionLoc *regionLoc, int tvoffset, int bvoffset, int lhoffset, int rhoffset)
{
	int f,c;

	int dstc, dstf;

	int fini = regionLoc->fini + tvoffset;
	int ffin = regionLoc->ffin - bvoffset;

	//int cini = regionLoc->cini;
	int cini = regionLoc->cini + lhoffset;
	int cfin = regionLoc->cfin - rhoffset;

	float * fila;
	for(f = fini, dstf = tvoffset; f <= ffin; dstf++, f++)
	{
		fila = region + dstf * w;
		for(c = cini, dstc = lhoffset; c <= cfin; dstc++, c++)
		{
			*(fila + dstc) = img[f][c];
		}

	}
}

#endif


#ifdef FILTRO_NOCACHE
static void aplicarFiltro_size5(float ** gsFiltradoM)
{
	unsigned int tamFiltro = G_tamFiltro;
	float ** auxM = G_bufferDeTrabajoM;
	float ** oM = G_copiaImagenM;
	float * hfiltro = G_filtroRuido1D;
	float * vfiltro = hfiltro;
	unsigned int height = G_height;
	unsigned int width = G_width;

#ifdef NEON
	float32x4_t nhfiltro, nvfiltro;
	nhfiltro = vld1q_f32(hfiltro);
	nvfiltro = vld1q_f32(vfiltro);
#endif
	//cambia
	int foffset = 2;
	float * centroFiltro = hfiltro + foffset;
	float * fp0 = centroFiltro-2,
	*fp1 = centroFiltro-1,
	*fp2 = centroFiltro,
	*fp3 = centroFiltro+1,
	*fp4 = centroFiltro+2;
	//fin-cambia
	int f;
	
	int maxHeight = height-foffset;
	int maxWidth = width-foffset;


	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime)
	for(f=0; f < height; f++)
	{
		int c;
		#ifdef NEON
		float * inioptr = oM[f];
		float * iniaptr = auxM[f];
		#endif
		for(c=foffset; c < maxWidth; c++)
		{
		#ifdef NEON
			float * sptr = inioptr + (c-2);
			float32x4_t tmp;
			tmp = vld1q_f32(sptr);
			tmp = vmulq_f32(tmp, nhfiltro);

			float32x2_t low = vget_low_f32(tmp);
			float32x2_t high = vget_high_f32(tmp);

			low = vadd_f32(low, high);

			float * dptr = iniaptr + c;
			*dptr = vget_lane_f32(low,0);
			*dptr += vget_lane_f32(low,1);

			*dptr += *(sptr+4)**fp4;

		#else
			float * dptr = &auxM[f][c];
			*dptr = 0;
			*dptr += oM[f][c-2]**fp0;
			*dptr += oM[f][c-1]**fp1;
			*dptr += oM[f][c]**fp2;
			*dptr += oM[f][c+1]**fp3;
			*dptr += oM[f][c+2]**fp4;

		#endif
		}
	}
	
	//cambia
	centroFiltro = vfiltro + foffset;
	fp0 = centroFiltro-2;
	fp1 = centroFiltro-1;
	fp2 = centroFiltro;
	fp3 = centroFiltro+1;
	fp4 = centroFiltro+2;
	//fin-cambia

#ifdef NEON_VF
	float32_t auxVector[4];
	#pragma omp parallel for schedule(runtime) private(auxVector)
#else
	#pragma omp parallel for schedule(runtime)
#endif
	for(f=foffset; f < maxHeight; f++)
	{
		int c;

		#ifdef NEON_VF
		float * iniaptr0 = auxM[f-2],
		*iniaptr1 = auxM[f-1],
		*iniaptr2 = auxM[f],
		*iniaptr3 = auxM[f+1],
		*iniaptr4 = auxM[f+2];
		#endif

		for(c=0; c < width; c++)
		{
		#ifdef NEON_VF
			auxVector[0] = *(iniaptr0 + c);
			auxVector[1] = *(iniaptr1 + c);
			auxVector[2] = *(iniaptr2 + c);
			auxVector[3] = *(iniaptr3 + c);

			float32x4_t tmp;
			tmp = vld1q_f32(auxVector);
			tmp = vmulq_f32(tmp, nvfiltro);
			
			float32x2_t low = vget_low_f32(tmp);
			float32x2_t high = vget_high_f32(tmp);

			low = vadd_f32(low, high);

			float * dptr = &gsFiltradoM[f][c];
			*dptr = vget_lane_f32(low,0);
			*dptr += vget_lane_f32(low,1);

			*dptr += *(iniaptr4+c)**fp4;



		#else
			float * dptr = &gsFiltradoM[f][c];
			*dptr = 0;
			*dptr += auxM[f-2][c]**fp0;
			*dptr += auxM[f-1][c]**fp1;
			*dptr += auxM[f][c]**fp2;
			*dptr += auxM[f+1][c]**fp3;
			*dptr += auxM[f+2][c]**fp4;
		#endif
		}
	}

}
#else
static void aplicarFiltro_size5(float ** gsFiltradoM)
{
	unsigned int tamFiltro = G_tamFiltro;
	float ** auxM = G_bufferDeTrabajoM;
	float ** oM = G_copiaImagenM;
	float * hfiltro = G_filtroRuido1D;
	float * vfiltro = hfiltro;
	unsigned int height = G_height;
	unsigned int width = G_width;

#ifdef NEON
	float32x4_t nhfiltro, nvfiltro;
	nhfiltro = vld1q_f32(hfiltro);
	nvfiltro = vld1q_f32(vfiltro);
#endif
	//cambia
	int foffset = 2;
	float * centroFiltroH = hfiltro + foffset;
	float * fph0 = centroFiltroH-2,
	*fph1 = centroFiltroH-1,
	*fph2 = centroFiltroH,
	*fph3 = centroFiltroH+1,
	*fph4 = centroFiltroH+2;
	//fin-cambia
	int f,c;
	
	int maxHeight = height-foffset;
	int maxWidth = width-foffset;

	unsigned int _h = REG_H;
	unsigned int _w = REG_W;
	unsigned int _size = _h*_w;

	unsigned int nregions;
	unsigned int regPerRow;

	RegionLoc * regions = getRegionsLoc(&nregions, _w, _h, width, height, 2, &regPerRow);


	float * centroFiltroV = vfiltro + foffset;
	float * fpv0 = centroFiltroV-2,
	*fpv1 = centroFiltroV-1,
	*fpv2 = centroFiltroV,
	*fpv3 = centroFiltroV+1,
	*fpv4 = centroFiltroV+2;

	int vfoffset = foffset*2;


	omp_set_num_threads(THREADS);
	RegionLoc *regLoc;

	float * _buffer = malloc(_size*sizeof(float)*nregions);

	int regionIdx;
#ifdef NEON_VF
	float32_t auxVector[4];
	#pragma omp parallel for schedule(runtime) private(f, c, regLoc, auxVector)
#else
	#pragma omp parallel for schedule(runtime) private(f, c, regLoc)
#endif
	for(regionIdx = 0; regionIdx < nregions; regionIdx++)
	{
		int regMaxRow, regMaxCol;
		regLoc = &regions[regionIdx];
		regMaxRow = regLoc->ffin-regLoc->fini;
		regMaxCol = regLoc->cfin-regLoc->cini-foffset;

		float * _region = _buffer + regLoc->idx * _size;

		getRegion(_region, _h, _w, oM, regLoc, 0, 0, 0, 0);
		//APLICAMOS EL FILTRO HORIZONTAL DONDE NO ESTE YA APLICADO
		for(f=0; f <= regMaxRow; f++)
		{
			float * _rfila = _region + _w * f;
			for(c=foffset; c <= regMaxCol; c++)
			{
				int _f,_c;
				_f = f + regLoc->fini;
				_c = c + regLoc->cini;

#ifdef NEON
				float * sptr = &oM[_f][_c-2];
				float32x4_t tmp;
				tmp = vld1q_f32(sptr);
				tmp = vmulq_f32(tmp, nhfiltro);

				float32x2_t low = vget_low_f32(tmp);
				float32x2_t high = vget_high_f32(tmp);

				low = vadd_f32(low, high);

				float * dptr = &auxM[_f][_c];
				*dptr = vget_lane_f32(low,0);
				*dptr += vget_lane_f32(low,1);

				*dptr += *(sptr+4)**fph4;
#else

				float * dptr = &auxM[_f][_c];
				float * rptr = _rfila + c-2;
				*dptr = 0;
				*dptr += *(rptr )**fph0;
				*dptr += *(rptr + 1)**fph1;
				*dptr += *(rptr + 2)**fph2;
				*dptr += *(rptr + 3)**fph3;
				*dptr += *(rptr + 4)**fph4;
#endif

			}
		}
		//APLICAMOS EL FILTRO VERTICAL DONDE NO ESTE YA APLICADO
		regMaxRow = regLoc->ffin - foffset;
		regMaxCol = regLoc->cfin - foffset;
		for(f=regLoc->fini+foffset; f <= regMaxRow; f++)
		{

			#ifdef NEON_VF
			float * iniaptr0 = auxM[f-2],
			*iniaptr1 = auxM[f-1],
			*iniaptr2 = auxM[f],
			*iniaptr3 = auxM[f+1],
			*iniaptr4 = auxM[f+2];
			#endif

			for(c=regLoc->cini+foffset; c <= regMaxCol; c++)
			{
			#ifdef NEON_VF
				auxVector[0] = *(iniaptr0 + c);
				auxVector[1] = *(iniaptr1 + c);
				auxVector[2] = *(iniaptr2 + c);
				auxVector[3] = *(iniaptr3 + c);

				float32x4_t tmp;
				tmp = vld1q_f32(auxVector);
				tmp = vmulq_f32(tmp, nvfiltro);

				float32x2_t low = vget_low_f32(tmp);
				float32x2_t high = vget_high_f32(tmp);

				low = vadd_f32(low, high);

				float * dptr = &gsFiltradoM[f][c];
				*dptr = vget_lane_f32(low,0);
				*dptr += vget_lane_f32(low,1);

				*dptr += *(iniaptr4+c)**fpv4;
			#else

				float * dptr = &gsFiltradoM[f][c];
				*dptr = 0;
				*dptr += auxM[f-2][c]**fpv0;
				*dptr += auxM[f-1][c]**fpv1;
				*dptr += auxM[f][c]**fpv2;
				*dptr += auxM[f+1][c]**fpv3;
				*dptr += auxM[f+2][c]**fpv4;
			#endif
			}
		}


	}


	free(regions);
	free(_buffer);

}
#endif

static void aplicarFiltro(float ** gsFiltradoM)
{

	unsigned int tamFiltro = G_tamFiltro;
	float ** auxM = G_bufferDeTrabajoM;
	float ** oM = G_copiaImagenM;
	float * hfiltro = G_filtroRuido1D;
	float * vfiltro = hfiltro;
	unsigned int height = G_height;
	unsigned int width = G_width;


	unsigned int foffset = tamFiltro >> 1;

	int f;
	
	int maxHeight = height-foffset;
	int maxWidth = width-foffset;

	//omp_set_num_threads(THREADS);
	//#pragma omp parallel for
	for(f=0; f < height; f++)
	{
		int c;
		for(c=foffset; c < maxWidth; c++)
		{
			
			float * fptr = &auxM[f][c];
			*fptr = 0;
			int c2, idx;
			for(idx = 0, c2 = c-foffset; idx < tamFiltro; idx++, c2++)
				*fptr += oM[f][c2] * hfiltro[idx];
		}
	}

	//#pragma omp parallel for
	for(f=foffset; f < maxHeight; f++)
	{
		int c;

		for(c=0; c < width; c++)
		{
			float * fptr = &gsFiltradoM[f][c];
			*fptr = 0;
			int f2, idx;
			for(idx = 0, f2 = f-foffset; idx < tamFiltro; idx++, f2++)
				*fptr += auxM[f2][c] * vfiltro[idx];

		}
	}

}
static uint8_t getDireccion(float rad)
{

	float deg = rad * RAD_2_DEG;

	if(deg < 0)
		deg = 180 + deg;

	float dif180 = fabsf(deg - 180);
	float dif135 = fabsf(deg - 135);
	float dif90 = fabsf(deg - 90);
	float dif45 = fabsf(deg - 45);
	float dif0 = deg;

	uint8_t res = 0;
	float diff = dif180;

	if(dif135 < diff){ res = 135; diff = dif135;}
	if(dif90 < diff){ res = 90; diff = dif90;}
	if(dif45 < diff){ res = 45; diff = dif45;}
	if(dif0 < diff){ res =  0; diff = dif0;}

	return res;
}

static void obtenerDireccionGradienteDiscreta(float * xg, float * yg, uint8_t * dgd, unsigned int width, unsigned int height)
{
	unsigned int length = width * height;
	int i;

	float vmax=0, vmin=0;
	omp_set_num_threads(THREADS);


	#pragma omp parallel for schedule(runtime) 
	for(i = 0 ; i < length; i++)
	{
		float x = xg[i];
		float y = yg[i];
#ifdef NO_DEG_LOOKUPTABLE
		float deg = atan(y/x);
		dgd[i] = getDireccion(deg);
#else

		dgd[i] =  LOOKUP_DEG[(int)round(y)+LOOKUPTABLE_DEG_VMAX][(int)round(x)+LOOKUPTABLE_DEG_VMAX];
#endif
	}

}

static void aplicarFiltroGradiente(float ** sM, float ** dM, float ** filtro, unsigned int height, unsigned int width)
{
	unsigned int foffset = 1;

	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int f;

#ifdef NEON
	float32x4_t ff0,ff1,ff2;
	ff0 = vld1q_f32(filtro[0]);
	ff1 = vld1q_f32(filtro[1]);
	ff2 = vld1q_f32(filtro[2]);
#endif

	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime)
	for(f=foffset; f < maxHeight; f++)
	{
		int c;
		for(c=foffset; c < maxWidth; c++)
		{
		#ifdef NEON
			int c0=c-1, c1=c, c2=c+1,
			f0=f-1, f1=f, f2=f+1;

			float32x4_t nsum, tmp, resf;
			nsum = vdupq_n_f32(0);

			float * cpixel;
			float * psM;
			cpixel = &dM[f][c];

			psM = &sM[f0][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f1][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f2][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(ff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			float32x2_t nsumlow = vget_low_f32(nsum);
			float32x2_t nsumhigh = vget_high_f32(nsum);

			*cpixel = vget_lane_f32(nsumlow,0);
			*cpixel += vget_lane_f32(nsumlow,1);	
			*cpixel += vget_lane_f32(nsumhigh,0);	

		#else
			int c0=c-1, c1=c, c2=c+1,
			f0=f-1, f1=f, f2=f+1;

			float * cpixel = &dM[f][c];
			
			*cpixel = 0;
			*cpixel += sM[f0][c0] * filtro[0][0];
			*cpixel += sM[f0][c1] * filtro[0][1];
			*cpixel += sM[f0][c2] * filtro[0][2];
			*cpixel += sM[f1][c0] * filtro[1][0];
			*cpixel += sM[f1][c1] * filtro[1][1];
			*cpixel += sM[f1][c2] * filtro[1][2];
			*cpixel += sM[f2][c0] * filtro[2][0];
			*cpixel += sM[f2][c1] * filtro[2][1];
			*cpixel += sM[f2][c2] * filtro[2][2];

		#endif
		}
	}

}

static void computeGradientX(float **imM, float ** gradientxM)
{
	float ** sM = imM;
	float ** dM = gradientxM;
	float ** filtro = G_filtroGradienteX;
	unsigned int height = G_height;
	unsigned int width = G_width;
	
	aplicarFiltroGradiente(sM, dM, filtro, height,  width);
}

static void computeGradientY(float** imM, float ** gradientyM)
{
	float ** sM = imM;
	float ** dM = gradientyM;
	float ** filtro = G_filtroGradienteY;
	unsigned int height = G_height;
	unsigned int width = G_width;

	aplicarFiltroGradiente(sM, dM, filtro, height,  width);
}

#ifdef GRAD_SEPARADAS_OPERACIONES 
static void _computeGradient(float ** sM, float ** xM, float **  yM, float ** xfiltro, float ** yfiltro, unsigned int height, unsigned int width)
{
	//cambia
	unsigned int foffset = 1;
	//fin-cambia

	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int f;

#ifdef NEON
	float32x4_t xff0,xff1,xff2, yff0, yff1, yff2;
	xff0 = vld1q_f32(xfiltro[0]);
	xff1 = vld1q_f32(xfiltro[1]);
	xff2 = vld1q_f32(xfiltro[2]);
	yff0 = vld1q_f32(yfiltro[0]);
	yff1 = vld1q_f32(yfiltro[1]);
	yff2 = vld1q_f32(yfiltro[2]);

#endif

	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime)
	for(f=foffset; f < maxHeight; f++)
	{
		int c;
		for(c=foffset; c < maxWidth; c++)
		{
		#ifdef NEON
			int c0=c-1, c1=c, c2=c+1,
			f0=f-1, f1=f, f2=f+1;

			//gradiente en X
			float32x4_t nsum, tmp, resf;
			nsum = vdupq_n_f32(0);

			float * cpixel;
			float * psM;
			cpixel = &xM[f][c];

			psM = &sM[f0][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f1][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f2][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			float32x2_t nsumlow = vget_low_f32(nsum);
			float32x2_t nsumhigh = vget_high_f32(nsum);

			*cpixel = vget_lane_f32(nsumlow,0);
			*cpixel += vget_lane_f32(nsumlow,1);	
			*cpixel += vget_lane_f32(nsumhigh,0);
			
			//gradiente en Y
			nsum = vdupq_n_f32(0);

			cpixel = &yM[f][c];

			psM = &sM[f0][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f1][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM = &sM[f2][c0];
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			nsumlow = vget_low_f32(nsum);
			nsumhigh = vget_high_f32(nsum);

			*cpixel = vget_lane_f32(nsumlow,0);
			*cpixel += vget_lane_f32(nsumlow,1);	
			*cpixel += vget_lane_f32(nsumhigh,0);	


		#else
			int c0=c-1, c1=c, c2=c+1,
			f0=f-1, f1=f, f2=f+1;

			//gradiente en X
			float * cpixel = &xM[f][c];
			
			*cpixel = 0;
			*cpixel += sM[f0][c0] * xfiltro[0][0];
			*cpixel += sM[f0][c1] * xfiltro[0][1];
			*cpixel += sM[f0][c2] * xfiltro[0][2];
			*cpixel += sM[f1][c0] * xfiltro[1][0];
			*cpixel += sM[f1][c1] * xfiltro[1][1];
			*cpixel += sM[f1][c2] * xfiltro[1][2];
			*cpixel += sM[f2][c0] * xfiltro[2][0];
			*cpixel += sM[f2][c1] * xfiltro[2][1];
			*cpixel += sM[f2][c2] * xfiltro[2][2];

			//gradiente en Y
			cpixel = &yM[f][c];
			
			*cpixel = 0;
			*cpixel += sM[f0][c0] * yfiltro[0][0];
			*cpixel += sM[f0][c1] * yfiltro[0][1];
			*cpixel += sM[f0][c2] * yfiltro[0][2];
			*cpixel += sM[f1][c0] * yfiltro[1][0];
			*cpixel += sM[f1][c1] * yfiltro[1][1];
			*cpixel += sM[f1][c2] * yfiltro[1][2];
			*cpixel += sM[f2][c0] * yfiltro[2][0];
			*cpixel += sM[f2][c1] * yfiltro[2][1];
			*cpixel += sM[f2][c2] * yfiltro[2][2];


		#endif
		}
	}


}

static void computeGradient(float ** imM, float ** gradientxM, float ** gradientyM)
{
	float ** sM = imM;
	float ** xM = gradientxM;
	float ** yM = gradientyM;
	float ** xfiltro = G_filtroGradienteX;
	float ** yfiltro = G_filtroGradienteY;
	unsigned int height = G_height;
	unsigned int width = G_width;
	
	_computeGradient(sM, xM, yM, xfiltro, yfiltro, height,  width);
}

#elif GRAD_NOCACHE
static void _computeGradient(float ** sM, float ** xM, float **  yM, float ** xfiltro, float ** yfiltro, float** mgM, uint8_t ** dgdM, unsigned int height, unsigned int width)
{
	//cambia
	unsigned int foffset = 1;
	//fin-cambia

	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int f;

#ifdef NEON
	float32x4_t xff0,xff1,xff2, yff0, yff1, yff2;
	xff0 = vld1q_f32(xfiltro[0]);
	xff1 = vld1q_f32(xfiltro[1]);
	xff2 = vld1q_f32(xfiltro[2]);
	yff0 = vld1q_f32(yfiltro[0]);
	yff1 = vld1q_f32(yfiltro[1]);
	yff2 = vld1q_f32(yfiltro[2]);

#endif
	float * mg;
	uint8_t * dgd;

	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime) private(mg, dgd)
	for(f=foffset; f < maxHeight; f++)
	{
		int c;

		mg = mgM[f];
		dgd = dgdM[f];

		#ifdef NEON
		int f0=f-1;
		float * inisptr = sM[f0];
		float * xptr = xM[f], *yptr = yM[f];
		#else
		int f0=f-1, f1=f, f2=f+1;
		#endif

		for(c=foffset; c < maxWidth; c++)
		{
		#ifdef NEON
			int c0=c-1;

			//gradiente en X
			float32x4_t nsum, tmp, resf;
			nsum = vdupq_n_f32(0);

			float * cpixel;
			float * psM, * colptr = inisptr + c0;
			cpixel = xptr + c;

			psM = colptr;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM += width;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM += width;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(xff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			float32x2_t nsumlow = vget_low_f32(nsum);
			float32x2_t nsumhigh = vget_high_f32(nsum);

			*cpixel = vget_lane_f32(nsumlow,0);
			*cpixel += vget_lane_f32(nsumlow,1);	
			*cpixel += vget_lane_f32(nsumhigh,0);
			float x = *cpixel;
			//gradiente en Y
			nsum = vdupq_n_f32(0);

			cpixel = yptr + c;

			psM = colptr;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff0, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM += width;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff1, tmp);
			nsum = vaddq_f32(nsum, resf);

			psM += width;
			tmp = vld1q_f32(psM);
			resf = vmulq_f32(yff2, tmp);
			nsum = vaddq_f32(nsum, resf);

			nsumlow = vget_low_f32(nsum);
			nsumhigh = vget_high_f32(nsum);

			*cpixel = vget_lane_f32(nsumlow,0);
			*cpixel += vget_lane_f32(nsumlow,1);	
			*cpixel += vget_lane_f32(nsumhigh,0);	
			float y = *cpixel;

		#else
			int c0=c-1, c1=c, c2=c+1;

			//gradiente en X
			float * cpixel = &xM[f][c];

			*cpixel = 0;
			*cpixel += sM[f0][c0] * xfiltro[0][0];
			*cpixel += sM[f0][c1] * xfiltro[0][1];
			*cpixel += sM[f0][c2] * xfiltro[0][2];
			*cpixel += sM[f1][c0] * xfiltro[1][0];
			*cpixel += sM[f1][c1] * xfiltro[1][1];
			*cpixel += sM[f1][c2] * xfiltro[1][2];
			*cpixel += sM[f2][c0] * xfiltro[2][0];
			*cpixel += sM[f2][c1] * xfiltro[2][1];
			*cpixel += sM[f2][c2] * xfiltro[2][2];
			float x = *cpixel;
			//gradiente en Y
			cpixel = &yM[f][c];


			*cpixel = 0;
			*cpixel += sM[f0][c0] * yfiltro[0][0];
			*cpixel += sM[f0][c1] * yfiltro[0][1];
			*cpixel += sM[f0][c2] * yfiltro[0][2];
			*cpixel += sM[f1][c0] * yfiltro[1][0];
			*cpixel += sM[f1][c1] * yfiltro[1][1];
			*cpixel += sM[f1][c2] * yfiltro[1][2];
			*cpixel += sM[f2][c0] * yfiltro[2][0];
			*cpixel += sM[f2][c1] * yfiltro[2][1];
			*cpixel += sM[f2][c2] * yfiltro[2][2];
			float y = *cpixel;

		#endif
			//Modulo del gradiente
			//mgM[f][c]=sqrt(x*x+y*y);
			*mg++ = sqrt(x*x+y*y);
			//Direccion del gradiente
		#ifdef NO_DEG_LOOKUPTABLE
			//dgdM[f][c] = getDireccion(deg);
			*dgd++ = getDireccion(atan(y/x));
		#else

			*dgd++ =  LOOKUP_DEG[(int)round(y)+LOOKUPTABLE_DEG_VMAX][(int)round(x)+LOOKUPTABLE_DEG_VMAX];
			//dgdM[f][c] =  LOOKUP_DEG[(int)round(y)+LOOKUPTABLE_DEG_VMAX][(int)round(x)+LOOKUPTABLE_DEG_VMAX];
		#endif

		}
	}


}

#else
static void _computeGradient(float ** sM, float ** xM, float **  yM, float ** xfiltro, float ** yfiltro, float** mgM, uint8_t ** dgdM, unsigned int height, unsigned int width)
{
	//cambia
	unsigned int foffset = 1;
	//fin-cambia

	unsigned int maxHeight = height-foffset;
	unsigned int maxWidth = width-foffset;
	int _f,_c;

#ifdef NEON
	float32x4_t xff0,xff1,xff2, yff0, yff1, yff2;
	xff0 = vld1q_f32(xfiltro[0]);
	xff1 = vld1q_f32(xfiltro[1]);
	xff2 = vld1q_f32(xfiltro[2]);
	yff0 = vld1q_f32(yfiltro[0]);
	yff1 = vld1q_f32(yfiltro[1]);
	yff2 = vld1q_f32(yfiltro[2]);

#endif

	unsigned int _h = REG_H;
	unsigned int _w = REG_W;
	unsigned int _size = _h*_w;

	unsigned int nregions;
	unsigned int regPerRow;

	RegionLoc * regions = getRegionsLoc(&nregions, _w, _h, width, height, foffset, &regPerRow);

	RegionLoc *regLoc, *maxRegionPtr;
//	maxRegionPtr = regions + nregions * sizeof(RegionLoc);

	float * _buffer = malloc(_size*sizeof(float)*nregions);

	float * mg;
	uint8_t * dgd;
	
	int regionIdx;

	omp_set_num_threads(THREADS);
	#pragma omp parallel for schedule(runtime) private(_f, _c, regLoc, mg, dgd)
	for(regionIdx = 0; regionIdx < nregions; regionIdx++)
	{
		int regMaxRow, regMaxCol;
		regLoc = &regions[regionIdx];
		regMaxRow = regLoc->ffin-regLoc->fini-foffset;
		regMaxCol = regLoc->cfin-regLoc->cini-foffset;

		float * _region = _buffer + regLoc->idx * _size;

		getRegion(_region, _h, _w, sM, regLoc, 0, 0, 0, 0);

		for(_f=foffset; _f <= regMaxRow; _f++)
		{
			int f,c;

			f = _f + regLoc->fini;

			mg = mgM[f] + regLoc->cini;
			dgd = dgdM[f] + regLoc->cini;

#ifdef NEON
			int f0=_f-1;
			float * filaSup = _region + _w*f0;
			float * xptr = xM[f], *yptr = yM[f];
#else
			int f0=_f-1, f1=_f, f2=_f+1;
#endif
			for(_c=foffset; _c <= regMaxCol; _c++)
			{
				c = _c + regLoc->cini;
#ifdef NEON
				//gradiente en X
				float32x4_t nsum, tmp, resf;
				nsum = vdupq_n_f32(0);

				float * cpixel;
				float * psM;
				cpixel = xptr + c;

				int c0 = _c -1;
				float *pini = filaSup + c0;

				psM = pini;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(xff0, tmp);
				nsum = vaddq_f32(nsum, resf);

				psM += _w;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(xff1, tmp);
				nsum = vaddq_f32(nsum, resf);

				psM += _w;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(xff2, tmp);
				nsum = vaddq_f32(nsum, resf);

				float32x2_t nsumlow = vget_low_f32(nsum);
				float32x2_t nsumhigh = vget_high_f32(nsum);

				*cpixel = vget_lane_f32(nsumlow,0);
				*cpixel += vget_lane_f32(nsumlow,1);
				*cpixel += vget_lane_f32(nsumhigh,0);
				float x = *cpixel;
				//gradiente en Y
				nsum = vdupq_n_f32(0);

				cpixel = yptr + c;

				psM = pini;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(yff0, tmp);
				nsum = vaddq_f32(nsum, resf);

				psM += _w;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(yff1, tmp);
				nsum = vaddq_f32(nsum, resf);

				psM += _w;
				tmp = vld1q_f32(psM);
				resf = vmulq_f32(yff2, tmp);
				nsum = vaddq_f32(nsum, resf);

				nsumlow = vget_low_f32(nsum);
				nsumhigh = vget_high_f32(nsum);

				*cpixel = vget_lane_f32(nsumlow,0);
				*cpixel += vget_lane_f32(nsumlow,1);
				*cpixel += vget_lane_f32(nsumhigh,0);
				float y = *cpixel;

#else
				int c0=_c-1, c1=_c, c2=_c+1;

				//gradiente en X
				float * cpixel = &xM[f][c];
				*cpixel = 0;
				*cpixel += *(_region + _w*f0 + c0) * xfiltro[0][0];
				*cpixel += *(_region + _w*f0 + c1) * xfiltro[0][1];
				*cpixel += *(_region + _w*f0 + c2) * xfiltro[0][2];
				*cpixel += *(_region + _w*f1 + c0) * xfiltro[1][0];
				*cpixel += *(_region + _w*f1 + c1) * xfiltro[1][1];
				*cpixel += *(_region + _w*f1 + c2) * xfiltro[1][2];
				*cpixel += *(_region + _w*f2 + c0) * xfiltro[2][0];
				*cpixel += *(_region + _w*f2 + c1) * xfiltro[2][1];
				*cpixel += *(_region + _w*f2 + c2) * xfiltro[2][2];
				float x = *cpixel;

				//gradiente en Y
				cpixel = &yM[f][c];
				*cpixel = 0;
				*cpixel += *(_region + _w*f0 + c0) * yfiltro[0][0];
				*cpixel += *(_region + _w*f0 + c1) * yfiltro[0][1];
				*cpixel += *(_region + _w*f0 + c2) * yfiltro[0][2];
				*cpixel += *(_region + _w*f1 + c0) * yfiltro[1][0];
				*cpixel += *(_region + _w*f1 + c1) * yfiltro[1][1];
				*cpixel += *(_region + _w*f1 + c2) * yfiltro[1][2];
				*cpixel += *(_region + _w*f2 + c0) * yfiltro[2][0];
				*cpixel += *(_region + _w*f2 + c1) * yfiltro[2][1];
				*cpixel += *(_region + _w*f2 + c2) * yfiltro[2][2];
				float y = *cpixel;
#endif
				//Modulo del gradiente
				*mg++ = sqrt(x*x+y*y);
#ifdef NO_DEG_LOOKUPTABLE
				float deg = atan(y/x);
				*dgd++ = getDireccion(atan(y/x));
#else

				*dgd++ =  LOOKUP_DEG[(int)round(y)+LOOKUPTABLE_DEG_VMAX][(int)round(x)+LOOKUPTABLE_DEG_VMAX];
#endif

			}
		}
	}

}

#endif


#ifndef GRAD_SEPARADAS_OPERACIONES
static void computeGradient(float ** imM, float ** gradientxM, float ** gradientyM, float** mgM, uint8_t ** dgdM)
{
	float ** sM = imM;
	float ** xM = gradientxM;
	float ** yM = gradientyM;
	float ** xfiltro = G_filtroGradienteX;
	float ** yfiltro = G_filtroGradienteY;
	unsigned int height = G_height;
	unsigned int width = G_width;

	_computeGradient(sM, xM, yM, xfiltro, yfiltro, mgM, dgdM, height,  width);
}
#endif




static void dibujarRectas(unsigned int ** accM, unsigned int nangulos, unsigned int ndistancias, unsigned int rhoOffset, uint8_t ** rectasM, float * sinTable, float * cosTable, unsigned int width, unsigned int height)
{
	int rhoIdx, thetaIdx;
	int maxTheta = 0 , maxRho = 0, maxCont = 0;
	for(rhoIdx=0; rhoIdx < ndistancias; rhoIdx++)
	{
		for(thetaIdx=0; thetaIdx < nangulos; thetaIdx++)
		{
			int cont = accM[rhoIdx][thetaIdx];
			if (cont > maxCont)
			{
				maxTheta = thetaIdx;
				maxRho = rhoIdx;
				maxCont = cont;
			}
		}
	}
	int x, y;
	float rho0 = maxRho - (float)rhoOffset;
	int xoffset = width >> 1;
	int yoffset = height >> 1;
	for(x = 0 ; x < width; x++)
	{
		y = (int)ceil( (float) (rho0 - (float)(x-xoffset) * cosTable[maxTheta]) / sinTable[maxTheta]);
		y += yoffset;
		rectasM[y][x] = 255;
	}
}

int main(int argc, char ** argv)
{
	int width, height, nangulos;
	int re; //indica si escribir por la salida estandar el pgm con los bordes
	unsigned int rgbLength, pixelLength;
	uint8_t *ppm, *rgb, *pgm, *gs;

	fprintf(stderr, "THREADS: %d\n", THREADS);
	int procs = omp_get_num_procs();
	int maxthreads = omp_get_max_threads();
	fprintf(stderr, "max procs: %d ; max threads: %d\n", procs, maxthreads);

#ifdef RASPI2
	fprintf(stderr, "RASPI2\n");
#endif
	
	float sigma;
	char * outdir;
	struct stat dirInfo;

	unsigned int lth, uth;

	unsigned int tamFiltro;
	getOptions(argc, argv, &tamFiltro, &sigma, &uth, &lth, &re, &nangulos, &outdir);

	if(stat(outdir, &dirInfo) == -1)
	{
		fprintf(stderr, "Error al obtener los metadatos del fichero \"%s\"\n", outdir);
		exit(1);
	}

	if(!S_ISDIR(dirInfo.st_mode))
	{
		fprintf(stderr, "%s no es un directorio\n", outdir);
		exit(2);
	}

	char filePath[256];
	strcpy(filePath,outdir);
	strcat(filePath,"/");

	int pathLength = strlen(filePath);
	
	char * outputFileName = filePath + pathLength;
	//Canny set up

#ifdef DEBUG
	fprintf(stderr, "Path: %s , pathLength: %d\n", filePath, pathLength);
	fprintf(stderr, "Sigma: %f , Size: %d\n", sigma, tamFiltro);
#endif
	//Hough set up
	float * cosTable, * sinTable;

	cosTable = (float * ) malloc(sizeof(float) * nangulos);
	sinTable = (float * ) malloc(sizeof(float) * nangulos);

	float theta;
	int thetaIndex;
	float thetaInc = M_PI / nangulos;

	int vtrue = 0;
	int vfalse = 255;

	for(theta = 0, thetaIndex = 0; thetaIndex < nangulos; theta += thetaInc, thetaIndex++)
	{
		cosTable[thetaIndex] = cos(theta);
		sinTable[thetaIndex] = sin(theta);
	}

#ifdef TIMMING
	struct timeval t0,t1;
	long long tacc = 0;
#endif

	if(read_ppmHeader(&width, &height) == 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Recibido ppm header\nEsperando RGB...\n");
#endif
		pixelLength = width * height;
		rgbLength = pixelLength  * 3;	

#ifdef DEBUG
		fprintf(stderr, "Width: %d , Height: %d\n", width, height);
#endif
		ppm = (uint8_t*) malloc(rgbLength+50);
	
		int ppmhl = sprintf((char*)ppm, "P6\n%d %d\n255\n", width, height);

		rgb = ppm +  ppmhl;

		unsigned int n = fread(rgb, rgbLength, 1, stdin);

#ifdef DEBUG
		fprintf(stderr, "Leido RGB: %d (%d bytes)\nConvirtiendo a GrayScale...\n", n, rgbLength*n);
#endif

		pgm = (uint8_t*) malloc(pixelLength+50);
		int pgmhl = sprintf((char*)pgm, "P5\n%d %d\n255\n", width, height);

		unsigned int ppmLength = ppmhl + rgbLength;
		unsigned int pgmLength = pgmhl + pixelLength;
		gs = pgm + pgmhl;
		rgb2gs(rgb, gs, width, height);

#ifdef SAVE_ORIG

		strcpy(outputFileName, "original-color.ppm");
		int fppm = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fppm < 0) showError();

		strcpy(outputFileName, "original-gs.pgm");
 		int fpgm = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fpgm < 0) showError();

		int res = write(fppm, ppm, ppmLength);
		if (res != ppmLength) showError(); 
		res = write(fpgm, pgm, pgmLength);
		if (res != pgmLength) showError(); 

		close(fppm);
		close(fpgm);
#endif
		unsigned int size = width * height;
		init(gs, width, height, sigma, tamFiltro);

		unsigned int floatLength = pixelLength * sizeof(float),
		uint8Length = pixelLength * sizeof(uint8_t),
		ptrUint8Length = pixelLength * sizeof(uint8_t*),
		ptrFloatLength = pixelLength * sizeof(float*);

		void * BUFFER = malloc(floatLength*5+uint8Length*3+ptrFloatLength*5+ptrUint8Length*3);
		float * gsFiltrado = (float*) (BUFFER);
		float ** gsFiltradoM = (float**) (gsFiltrado+pixelLength);
		float * xgradiente = (float*) (gsFiltradoM+height);
		float ** xgradienteM = (float**) (xgradiente+pixelLength);
		float * ygradiente = (float*) (xgradienteM+height);
		float ** ygradienteM = (float**) (ygradiente+pixelLength);
		float * mgradiente = (float*) (ygradienteM+height);
		float ** mgradienteM = (float**) (mgradiente+pixelLength);
		uint8_t * dgdiscreta = (uint8_t*) (mgradienteM+height);
		uint8_t ** dgdiscretaM = (uint8_t**) (dgdiscreta+pixelLength);
		float * mgthin = (float*) (dgdiscretaM+height);
		float ** mgthinM = (float**) (mgthin+pixelLength);
		uint8_t * mgthinescalado = (uint8_t *) (mgthinM+height);
		uint8_t ** mgthinescaladoM = (uint8_t**) (mgthinescalado+pixelLength);
		uint8_t * bordes = (uint8_t *) (mgthinescaladoM+height);
		uint8_t ** bordesM = (uint8_t**) (bordes+pixelLength);

		getMatrixFromArray_Float_buffer(gsFiltrado, width, height, size, gsFiltradoM);
		getMatrixFromArray_Float_buffer(xgradiente, width, height, size, xgradienteM);
		getMatrixFromArray_Float_buffer(ygradiente, width, height, size, ygradienteM);
		getMatrixFromArray_Float_buffer(mgradiente, width, height, size, mgradienteM);
		getMatrixFromArray_Float_buffer(mgthin, width, height, size, mgthinM);
		getMatrixFromArray_Uint8_buffer(dgdiscreta, width, height, size, dgdiscretaM);
		getMatrixFromArray_Uint8_buffer(bordes, width, height, size, bordesM);
		getMatrixFromArray_Uint8_buffer(mgthinescalado, width, height, size, mgthinescaladoM);

		copiarArray_Uint8_Float(G_copiaImagen, gs, size);
		memcpy(G_bufferDeTrabajo, G_copiaImagen, size*sizeof(float));
		memcpy(gsFiltrado, G_copiaImagen, size*sizeof(float));

#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif 
		//Filtramos el ruido con la gausiana especificada por el usuario
		#ifdef NOISEFILTER_NOSEPARABLE

		#ifdef FS_5
		aplicarFiltro_noSeparable_size5(gsFiltradoM);
		#else
		aplicarFiltro_noSeparable(gsFiltradoM);
		#endif

		#else

		#ifdef FS_5
		aplicarFiltro_size5(gsFiltradoM);
		#else
		aplicarFiltro(gsFiltradoM);
		#endif

		#endif
#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("01-Filtrada", &t0,&t1,&tacc);
#endif
	
#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif
	#ifdef GRAD_SEPARADAS_OPERACIONES 
		#ifndef GRAD_SEPARADOS		
			//Obtenemos el cambio de intensidad del gradiente en X
			#ifdef GRAD_SEPARABLE
			//TODO:...
			#else
			computeGradient(gsFiltradoM, xgradienteM, ygradienteM);
			#endif
		#else
			//Obtenemos el cambio de intensidad del gradiente en X
			#ifdef GRAD_SEPARABLE
			//TODO:...
			#else
			computeGradientX(gsFiltradoM, xgradienteM);
			#endif

		#endif
	#else
		computeGradient(gsFiltradoM, xgradienteM, ygradienteM, mgradienteM, dgdiscretaM);
		
	#endif

#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("02-Gradiente en X", &t0,&t1,&tacc);
#endif

#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif


	#ifdef GRAD_SEPARADAS_OPERACIONES
		#ifdef GRAD_SEPARADOS		
			//Obtenemos el cambio de intensidad del gradiente en Y
			#ifdef GRAD_SEPARABLE
			//TODO:...
			#else
			computeGradientY(gsFiltradoM, ygradienteM);
			#endif

		#endif
	#endif



#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("03-Gradiente en Y", &t0,&t1,&tacc);
#endif
#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif
		//Obtenemos el modulo del gradiente

	#ifdef GRAD_SEPARADAS_OPERACIONES
		obtenerModuloGradiente(xgradiente, ygradiente, mgradiente, width, height);
	#endif
#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("04-Modulo gradiente", &t0,&t1,&tacc);
#endif
#ifdef TIMMING

		//gettimeofday(&t0, NULL);
#endif
		//Obtenemos la direccion del gradiente
		//obtenerDireccionGradiente(xgradiente, ygradiente, dgradiente, width, height);
#ifdef TIMMING
		//gettimeofday(&t1, NULL);
		//mostrarTiempo("05-Direccion gradiente", &t0,&t1,&tacc);
		fprintf(stderr,"06-Direccion gradiente:\t0 us\n");

#endif
#ifdef TIMMING

		gettimeofday(&t0, NULL);
#endif

		//Discretizamos la direccion del gradiente en 4 direcciones (0, 45, 90 y 135º), es decir: 0, 85, 170 y 255
		//discretizarDireccionGradiente(dgradiente, dgdiscreta, width, height);
	#ifdef GRAD_SEPARADAS_OPERACIONES
		obtenerDireccionGradienteDiscreta(xgradiente, ygradiente, dgdiscreta, width, height);
	#endif

#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("06-Direccion gradiente discreta", &t0,&t1,&tacc);

#endif

#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif
		//Supresion de los no maximos (para adelgazar los bordes resaltados antes de la umbralizacion)
		nonMaximum(mgradienteM, dgdiscretaM, mgthinM, width, height);
#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("07-Supresion no maximos", &t0,&t1,&tacc);
#endif

#ifdef SAVE_STEPS
		uint8_t * xgescalado = (uint8_t*) malloc(pixelLength);
		uint8_t * ygescalado = (uint8_t*) malloc(pixelLength);
		uint8_t * mgescalado = (uint8_t*) malloc(pixelLength);
		uint8_t * dgescalado = (uint8_t*) malloc(pixelLength);
		uint8_t * dgdescalado = (uint8_t*) malloc(pixelLength);

		escalar_Float_Uint8(xgradiente, xgescalado, pixelLength);
		escalar_Float_Uint8(ygradiente, ygescalado, pixelLength);
		escalar_Float_Uint8(mgradiente, mgescalado, pixelLength);
		escalar_Uint8_Uint8(dgdiscreta, dgdescalado, pixelLength);
#endif

		escalar_Float_Uint8(mgthin, mgthinescalado, pixelLength);
#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif

		//Paso final de Canny: Umbralizacion
		hysteresis(mgthinescaladoM, bordesM, uth, lth, width, height);
#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("08-Umbralizacion", &t0,&t1,&tacc);

#endif

#ifdef DEBUG
		fprintf(stderr, "alto: %d , bajo: %d\n", uth, lth);
#endif

#ifdef SAVE_STEPS
		strcpy(outputFileName, "01-filtrada.pgm");
		int ffiltrado = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (ffiltrado < 0) showError();


		strcpy(outputFileName, "02-xgradiente.pgm");
 		int fxgradiente = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fxgradiente < 0) showError();

		strcpy(outputFileName, "03-ygradiente.pgm");
		int fygradiente = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fygradiente < 0) showError();

		strcpy(outputFileName, "04-mgradiente.pgm");
		int fmgradiente = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fmgradiente < 0) showError();


		strcpy(outputFileName, "06-dgradiente-discreta.pgm");
		int fdgradiente_discreta = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fdgradiente_discreta < 0) showError();

		strcpy(outputFileName, "07-mgradiente-nonmaximum.pgm");
		int fmgthin = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fmgthin < 0) showError();

		strcpy(outputFileName, "08-bordes.pgm");
		int fbordes = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fbordes < 0) showError();



		saveImage_Float(ffiltrado, pgm, pgmhl, gsFiltrado, pixelLength);
		saveImage(fxgradiente, pgm, pgmhl, xgescalado, pixelLength);
		free(xgescalado);
		saveImage(fygradiente, pgm, pgmhl, ygescalado, pixelLength);
		free(ygescalado);
		saveImage(fmgradiente, pgm, pgmhl, mgescalado, pixelLength);
		free(mgescalado);
		saveImage(fdgradiente_discreta, pgm, pgmhl, dgdescalado, pixelLength);
		free(dgdescalado);
		saveImage(fmgthin, pgm, pgmhl, mgthinescalado, pixelLength);
		saveImage(fbordes, pgm, pgmhl, bordes, pixelLength);

		close(ffiltrado);
		close(fxgradiente);
		close(fygradiente);
		close(fmgradiente);
		close(fdgradiente_discreta);
		close(fmgthin);
		close(fbordes);

#endif

		free(ppm);


		//FIN CANNY
		//HOUGH
		unsigned int rhoMax = sqrt(width*width + height*height);
		unsigned int ndistancias = rhoMax;

#ifdef DEBUG
		fprintf(stderr, "Angulos: %d, Distancias: %d\n", nangulos, ndistancias);
#endif

		unsigned int houghSpLength = ndistancias*nangulos;

		//Matriz de acumuladores
		unsigned int ** accM = (unsigned int**) malloc (sizeof(unsigned int*) * ndistancias);
		unsigned int * acc = (unsigned int*) malloc(sizeof(unsigned int) * nangulos * ndistancias);

		int fila;
		for(fila = 0; fila < ndistancias ; fila++)
		{
			accM[fila] = acc + fila * nangulos;
			unsigned int * ptr, *mptr = accM[fila] + nangulos;
			for(ptr = accM[fila]; ptr < mptr; ptr++)
			{
				*ptr = 0;
			}

		}

#ifdef TIMMING
		gettimeofday(&t0, NULL);
#endif
		unsigned int rhoOffset = ndistancias >> 1;

		unsigned int width2 = width >> 1;
		unsigned int height2 = height >> 1;	
		omp_set_num_threads(THREADS);
		int col;
		#pragma omp parallel for schedule(runtime) private(col)
		for(fila = 0; fila < height; fila++)
		{
			for(col = 0; col < width; col++)
			{
				if(bordesM[fila][col] == 255) //255: pertenece a borde, 0: no pertenece a borde
				{
					houghAcc(col, fila, accM, sinTable, cosTable, width2, height2, nangulos, rhoOffset, vtrue);
				}
			}
		}
#ifdef TIMMING
		gettimeofday(&t1, NULL);
		mostrarTiempo("09-Obtencion Hough space", &t0,&t1,&tacc);
		fprintf(stderr, "TOTAL:\t%lld us\n", tacc);
#endif

#ifndef NORESULT
		pgmhl = sprintf((char*)pgm, "P5\n%d %d\n255\n", nangulos, ndistancias);
		uint8_t * houghSpAccEscalado = (uint8_t *) malloc(houghSpLength);

		escalar_Int_Uint8(acc, houghSpAccEscalado, houghSpLength);

		strcpy(outputFileName, "10-houghSpAcc.pgm");

		int fhoughSpAcc = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (fhoughSpAcc < 0) showError();

		invertirValores(houghSpAccEscalado, houghSpLength);
		saveImage(fhoughSpAcc, pgm, pgmhl, houghSpAccEscalado, houghSpLength);

		close(fhoughSpAcc);
		free(houghSpAccEscalado);
		if(re)
			saveImage(1, pgm, pgmhl, houghSpAccEscalado, houghSpLength);

		uint8_t * rectas = (uint8_t*) malloc(pixelLength);
		uint8_t ** rectasM = (uint8_t**) malloc(height*sizeof(uint8_t*));

		getMatrixFromArray_Uint8_buffer(rectas, width, height, pixelLength, rectasM);

		memset(rectas, 0, pixelLength);
		dibujarRectas(accM, nangulos, ndistancias, rhoOffset, rectasM, sinTable, cosTable, width, height);

		pgmhl = sprintf((char*)pgm, "P5\n%d %d\n255\n", width, height);
		strcpy(outputFileName, "11-rectas.pgm");
		int frectas = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH );
		if (frectas < 0) showError();

		saveImage(frectas, pgm, pgmhl, rectas, pixelLength);
#endif


		free(accM);
		free(acc);
		free(pgm);

		free(BUFFER);
	}
	free(cosTable);
	free(sinTable);
	freeAll();
	return 0;
}


