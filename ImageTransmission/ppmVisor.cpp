#include <cstdio>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>

#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace cv;

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
read_header(int *w, int *h)
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


int main(int argc, char ** argv)
{

	int width, height;
	unsigned int rgbLength;
	uint8_t *ppm, *rgb;

	namedWindow( "IRS ROV Camera", WINDOW_AUTOSIZE );// Create a window for display.
	while(true)
	{
		if(read_header(&width, &height) == 0)
		{
			std::cout << "VISOR: Recibido ppm header" << std::endl;
			rgbLength = width * height  * 3;	

			ppm = (uint8_t*) malloc(rgbLength+50);
			
			int hl = sprintf((char*)ppm, "P6\n%d %d\n255\n", width, height);

			rgb = ppm +  hl;
			unsigned int n = read(0, rgb, rgbLength);
			std::cout << "VISOR: Recibido RGB: " << n << " bytes" <<std::endl;
			
			std::vector<uint8_t> ppmv(ppm, rgb+rgbLength);
			std::cout << ppmv.size();

			Mat image = imdecode(ppmv,CV_LOAD_IMAGE_COLOR);

			namedWindow( "IRS ROV Camera", WINDOW_NORMAL );// Create a window for display.
			imshow("IRS ROV Camera", image);
			waitKey(1);

			free(ppm);
		}
	}
	return 0;
}
