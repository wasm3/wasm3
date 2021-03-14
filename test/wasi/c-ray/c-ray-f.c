/* c-ray-f - a simple raytracing filter.
 * Copyright (C) 2006 John Tsiombikas <nuclear@siggraph.org>
 *
 * You are free to use, modify and redistribute this program under the
 * terms of the GNU General Public License v2 or (at your option) later.
 * see "http://www.gnu.org/licenses/gpl.txt" for details.
 * ---------------------------------------------------------------------
 * Usage:
 *   compile:  cc -o c-ray-f c-ray-f.c -lm
 *   run:      cat scene | ./c-ray-f >foo.ppm
 *   enjoy:    display foo.ppm (with imagemagick)
 *      or:    imgview foo.ppm (on IRIX)
 * ---------------------------------------------------------------------
 * Scene file format:
 *   # sphere (many)
 *   s  x y z  rad   r g b   shininess   reflectivity
 *   # light (many)
 *   l  x y z
 *   # camera (one)
 *   c  x y z  fov   tx ty tz
 * ---------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>

/* find the appropriate way to define explicitly sized types */
#if (__STDC_VERSION__ >= 199900) || defined(__GLIBC__)	/* C99 or GNU libc */
#include <stdint.h>
#elif defined(__unix__) || defined(unix)
#include <sys/types.h>
#elif defined(_MSC_VER)	/* the nameless one */
typedef unsigned __int8 uint8_t;
typedef unsigned __int32 uint32_t;
#endif

struct vec3 {
	double x, y, z;
};

struct ray {
	struct vec3 orig, dir;
};

struct material {
	struct vec3 col;	/* color */
	double spow;		/* specular power */
	double refl;		/* reflection intensity */
};

struct sphere {
	struct vec3 pos;
	double rad;
	struct material mat;
	struct sphere *next;
};

struct spoint {
	struct vec3 pos, normal, vref;	/* position, normal and view reflection */
	double dist;		/* parametric distance of intersection along the ray */
};

struct camera {
	struct vec3 pos, targ;
	double fov;
};

void render(int xsz, int ysz, uint32_t *fb, int samples);
struct vec3 trace(struct ray ray, int depth);
struct vec3 shade(struct sphere *obj, struct spoint *sp, int depth);
struct vec3 reflect(struct vec3 v, struct vec3 n);
struct vec3 cross_product(struct vec3 v1, struct vec3 v2);
struct ray get_primary_ray(int x, int y, int sample);
struct vec3 get_sample_pos(int x, int y, int sample);
struct vec3 jitter(int x, int y, int s);
int ray_sphere(const struct sphere *sph, struct ray ray, struct spoint *sp);
void load_scene(FILE *fp);
unsigned long get_msec(void);

#define MAX_LIGHTS		16				/* maximum number of lights */
#define RAY_MAG			1000.0			/* trace rays of this magnitude */
#define MAX_RAY_DEPTH	5				/* raytrace recursion limit */
#define FOV				0.78539816		/* field of view in rads (pi/4) */
#define HALF_FOV		(FOV * 0.5)
#define ERR_MARGIN		1e-6			/* an arbitrary error margin to avoid surface acne */

/* bit-shift amount for packing each color into a 32bit uint */
#ifdef LITTLE_ENDIAN
#define RSHIFT	16
#define BSHIFT	0
#else	/* big endian */
#define RSHIFT	0
#define BSHIFT	16
#endif	/* endianness */
#define GSHIFT	8	/* this is the same in both byte orders */

/* some helpful macros... */
#define SQ(x)		((x) * (x))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define DOT(a, b)	((a).x * (b).x + (a).y * (b).y + (a).z * (b).z)
#define NORMALIZE(a)  do {\
	double len = sqrt(DOT(a, a));\
	(a).x /= len; (a).y /= len; (a).z /= len;\
} while(0);

/* global state */
int xres = 800;
int yres = 600;
double aspect = 1.333333;
struct sphere *obj_list;
struct vec3 lights[MAX_LIGHTS];
int lnum = 0;
struct camera cam;

#define NRAN	1024
#define MASK	(NRAN - 1)
struct vec3 urand[NRAN];
int irand[NRAN];

const char *usage = {
	"Usage: c-ray-f [options]\n"
	"  Reads a scene file from stdin, writes the image to stdout, and stats to stderr.\n\n"
	"Options:\n"
	"  -s WxH     where W is the width and H the height of the image\n"
	"  -r <rays>  shoot <rays> rays per pixel (antialiasing)\n"
	"  -i <file>  read from <file> instead of stdin\n"
	"  -o <file>  write to <file> instead of stdout\n"
	"  -h         this help screen\n\n"
};



int main(int argc, char **argv) {
	int i;
	unsigned long rend_time, start_time;
	uint32_t *pixels;
	int rays_per_pixel = 1;
	FILE *infile = stdin, *outfile = stdout;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-' && argv[i][2] == 0) {
			char *sep;
			switch(argv[i][1]) {
			case 's':
				if(!isdigit(argv[++i][0]) || !(sep = strchr(argv[i], 'x')) || !isdigit(*(sep + 1))) {
					fputs("-s must be followed by something like \"640x480\"\n", stderr);
					return EXIT_FAILURE;
				}
				xres = atoi(argv[i]);
				yres = atoi(sep + 1);
				aspect = (double)xres / (double)yres;
				break;

			case 'i':
				if(!(infile = fopen(argv[++i], "r"))) {
					fprintf(stderr, "failed to open input file %s: %s\n", argv[i], strerror(errno));
					return EXIT_FAILURE;
				}
				break;

			case 'o':
				if(!(outfile = fopen(argv[++i], "w"))) {
					fprintf(stderr, "failed to open output file %s: %s\n", argv[i], strerror(errno));
					return EXIT_FAILURE;
				}
				break;

			case 'r':
				if(!isdigit(argv[++i][0])) {
					fputs("-r must be followed by a number (rays per pixel)\n", stderr);
					return EXIT_FAILURE;
				}
				rays_per_pixel = atoi(argv[i]);
				break;

			case 'h':
				fputs(usage, stdout);
				return 0;
				
			default:
				fprintf(stderr, "unrecognized argument: %s\n", argv[i]);
				fputs(usage, stderr);
				return EXIT_FAILURE;
			}
		} else {
			fprintf(stderr, "unrecognized argument: %s\n", argv[i]);
			fputs(usage, stderr);
			return EXIT_FAILURE;
		}
	}

	if(!(pixels = malloc(xres * yres * sizeof *pixels))) {
		perror("pixel buffer allocation failed");
		return EXIT_FAILURE;
	}
	load_scene(infile);

	/* initialize the random number tables for the jitter */
	for(i=0; i<NRAN; i++) urand[i].x = (double)rand() / RAND_MAX - 0.5;
	for(i=0; i<NRAN; i++) urand[i].y = (double)rand() / RAND_MAX - 0.5;
	for(i=0; i<NRAN; i++) irand[i] = (int)(NRAN * ((double)rand() / RAND_MAX));
	
	start_time = get_msec();
	render(xres, yres, pixels, rays_per_pixel);
	rend_time = get_msec() - start_time;
	
	/* output statistics to stderr */
	fprintf(stderr, "Rendering took: %lu seconds (%lu milliseconds)\n", rend_time / 1000, rend_time);

	/* output the image */
	fprintf(outfile, "P6\n%d %d\n255\n", xres, yres);
	for(i=0; i<xres * yres; i++) {
		fputc((pixels[i] >> RSHIFT) & 0xff, outfile);
		fputc((pixels[i] >> GSHIFT) & 0xff, outfile);
		fputc((pixels[i] >> BSHIFT) & 0xff, outfile);
	}
	fflush(outfile);

	if(infile != stdin) fclose(infile);
	if(outfile != stdout) fclose(outfile);
	return 0;
}

/* render a frame of xsz/ysz dimensions into the provided framebuffer */
void render(int xsz, int ysz, uint32_t *fb, int samples) {
	int i, j, s;
	double rcp_samples = 1.0 / (double)samples;

	/* for each subpixel, trace a ray through the scene, accumulate the
	 * colors of the subpixels of each pixel, then pack the color and
	 * put it into the framebuffer.
	 * XXX: assumes contiguous scanlines with NO padding, and 32bit pixels.
	 */
	for(j=0; j<ysz; j++) {
		for(i=0; i<xsz; i++) {
			double r, g, b;
			r = g = b = 0.0;
			
			for(s=0; s<samples; s++) {
				struct vec3 col = trace(get_primary_ray(i, j, s), 0);
				r += col.x;
				g += col.y;
				b += col.z;
			}

			r = r * rcp_samples;
			g = g * rcp_samples;
			b = b * rcp_samples;
				
			*fb++ = ((uint32_t)(MIN(r, 1.0) * 255.0) & 0xff) << RSHIFT |
					((uint32_t)(MIN(g, 1.0) * 255.0) & 0xff) << GSHIFT |
					((uint32_t)(MIN(b, 1.0) * 255.0) & 0xff) << BSHIFT;
		}
	}
}

/* trace a ray through the scene recursively (the recursion happens through
 * shade() to calculate reflection rays if necessary).
 */
struct vec3 trace(struct ray ray, int depth) {
	struct vec3 col;
	struct spoint sp, nearest_sp;
	struct sphere *nearest_obj = 0;
	struct sphere *iter = obj_list->next;

	/* if we reached the recursion limit, bail out */
	if(depth >= MAX_RAY_DEPTH) {
		col.x = col.y = col.z = 0.0;
		return col;
	}
	
	/* find the nearest intersection ... */
	while(iter) {
		if(ray_sphere(iter, ray, &sp)) {
			if(!nearest_obj || sp.dist < nearest_sp.dist) {
				nearest_obj = iter;
				nearest_sp = sp;
			}
		}
		iter = iter->next;
	}

	/* and perform shading calculations as needed by calling shade() */
	if(nearest_obj) {
		col = shade(nearest_obj, &nearest_sp, depth);
	} else {
		col.x = col.y = col.z = 0.0;
	}

	return col;
}

/* Calculates direct illumination with the phong reflectance model.
 * Also handles reflections by calling trace again, if necessary.
 */
struct vec3 shade(struct sphere *obj, struct spoint *sp, int depth) {
	int i;
	struct vec3 col = {0, 0, 0};

	/* for all lights ... */
	for(i=0; i<lnum; i++) {
		double ispec, idiff;
		struct vec3 ldir;
		struct ray shadow_ray;
		struct sphere *iter = obj_list->next;
		int in_shadow = 0;

		ldir.x = lights[i].x - sp->pos.x;
		ldir.y = lights[i].y - sp->pos.y;
		ldir.z = lights[i].z - sp->pos.z;

		shadow_ray.orig = sp->pos;
		shadow_ray.dir = ldir;

		/* shoot shadow rays to determine if we have a line of sight with the light */
		while(iter) {
			if(ray_sphere(iter, shadow_ray, 0)) {
				in_shadow = 1;
				break;
			}
			iter = iter->next;
		}

		/* and if we're not in shadow, calculate direct illumination with the phong model. */
		if(!in_shadow) {
			NORMALIZE(ldir);

			idiff = MAX(DOT(sp->normal, ldir), 0.0);
			ispec = obj->mat.spow > 0.0 ? pow(MAX(DOT(sp->vref, ldir), 0.0), obj->mat.spow) : 0.0;

			col.x += idiff * obj->mat.col.x + ispec;
			col.y += idiff * obj->mat.col.y + ispec;
			col.z += idiff * obj->mat.col.z + ispec;
		}
	}

	/* Also, if the object is reflective, spawn a reflection ray, and call trace()
	 * to calculate the light arriving from the mirror direction.
	 */
	if(obj->mat.refl > 0.0) {
		struct ray ray;
		struct vec3 rcol;

		ray.orig = sp->pos;
		ray.dir = sp->vref;
		ray.dir.x *= RAY_MAG;
		ray.dir.y *= RAY_MAG;
		ray.dir.z *= RAY_MAG;

		rcol = trace(ray, depth + 1);
		col.x += rcol.x * obj->mat.refl;
		col.y += rcol.y * obj->mat.refl;
		col.z += rcol.z * obj->mat.refl;
	}

	return col;
}

/* calculate reflection vector */
struct vec3 reflect(struct vec3 v, struct vec3 n) {
	struct vec3 res;
	double dot = v.x * n.x + v.y * n.y + v.z * n.z;
	res.x = -(2.0 * dot * n.x - v.x);
	res.y = -(2.0 * dot * n.y - v.y);
	res.z = -(2.0 * dot * n.z - v.z);
	return res;
}

struct vec3 cross_product(struct vec3 v1, struct vec3 v2) {
	struct vec3 res;
	res.x = v1.y * v2.z - v1.z * v2.y;
	res.y = v1.z * v2.x - v1.x * v2.z;
	res.z = v1.x * v2.y - v1.y * v2.x;
	return res;
}

/* determine the primary ray corresponding to the specified pixel (x, y) */
struct ray get_primary_ray(int x, int y, int sample) {
	struct ray ray;
	float m[3][3];
	struct vec3 i, j = {0, 1, 0}, k, dir, orig, foo;

	k.x = cam.targ.x - cam.pos.x;
	k.y = cam.targ.y - cam.pos.y;
	k.z = cam.targ.z - cam.pos.z;
	NORMALIZE(k);

	i = cross_product(j, k);
	j = cross_product(k, i);
	m[0][0] = i.x; m[0][1] = j.x; m[0][2] = k.x;
	m[1][0] = i.y; m[1][1] = j.y; m[1][2] = k.y;
	m[2][0] = i.z; m[2][1] = j.z; m[2][2] = k.z;
	
	ray.orig.x = ray.orig.y = ray.orig.z = 0.0;
	ray.dir = get_sample_pos(x, y, sample);
	ray.dir.z = 1.0 / HALF_FOV;
	ray.dir.x *= RAY_MAG;
	ray.dir.y *= RAY_MAG;
	ray.dir.z *= RAY_MAG;
	
	dir.x = ray.dir.x + ray.orig.x;
	dir.y = ray.dir.y + ray.orig.y;
	dir.z = ray.dir.z + ray.orig.z;
	foo.x = dir.x * m[0][0] + dir.y * m[0][1] + dir.z * m[0][2];
	foo.y = dir.x * m[1][0] + dir.y * m[1][1] + dir.z * m[1][2];
	foo.z = dir.x * m[2][0] + dir.y * m[2][1] + dir.z * m[2][2];

	orig.x = ray.orig.x * m[0][0] + ray.orig.y * m[0][1] + ray.orig.z * m[0][2] + cam.pos.x;
	orig.y = ray.orig.x * m[1][0] + ray.orig.y * m[1][1] + ray.orig.z * m[1][2] + cam.pos.y;
	orig.z = ray.orig.x * m[2][0] + ray.orig.y * m[2][1] + ray.orig.z * m[2][2] + cam.pos.z;

	ray.orig = orig;
	ray.dir.x = foo.x + orig.x;
	ray.dir.y = foo.y + orig.y;
	ray.dir.z = foo.z + orig.z;
	
	return ray;
}


struct vec3 get_sample_pos(int x, int y, int sample) {
	struct vec3 pt;
	double xsz = 2.0, ysz = xres / aspect;
	static double sf = 0.0;

	if(sf == 0.0) {
		sf = 2.0 / (double)xres;
	}

	pt.x = ((double)x / (double)xres) - 0.5;
	pt.y = -(((double)y / (double)yres) - 0.65) / aspect;

	if(sample) {
		struct vec3 jt = jitter(x, y, sample);
		pt.x += jt.x * sf;
		pt.y += jt.y * sf / aspect;
	}
	return pt;
}

/* jitter function taken from Graphics Gems I. */
struct vec3 jitter(int x, int y, int s) {
	struct vec3 pt;
	pt.x = urand[(x + (y << 2) + irand[(x + s) & MASK]) & MASK].x;
	pt.y = urand[(y + (x << 2) + irand[(y + s) & MASK]) & MASK].y;
	return pt;
}

/* Calculate ray-sphere intersection, and return {1, 0} to signify hit or no hit.
 * Also the surface point parameters like position, normal, etc are returned through
 * the sp pointer if it is not NULL.
 */
int ray_sphere(const struct sphere *sph, struct ray ray, struct spoint *sp) {
	double a, b, c, d, sqrt_d, t1, t2;
	
	a = SQ(ray.dir.x) + SQ(ray.dir.y) + SQ(ray.dir.z);
	b = 2.0 * ray.dir.x * (ray.orig.x - sph->pos.x) +
				2.0 * ray.dir.y * (ray.orig.y - sph->pos.y) +
				2.0 * ray.dir.z * (ray.orig.z - sph->pos.z);
	c = SQ(sph->pos.x) + SQ(sph->pos.y) + SQ(sph->pos.z) +
				SQ(ray.orig.x) + SQ(ray.orig.y) + SQ(ray.orig.z) +
				2.0 * (-sph->pos.x * ray.orig.x - sph->pos.y * ray.orig.y - sph->pos.z * ray.orig.z) - SQ(sph->rad);
	
	if((d = SQ(b) - 4.0 * a * c) < 0.0) return 0;

	sqrt_d = sqrt(d);
	t1 = (-b + sqrt_d) / (2.0 * a);
	t2 = (-b - sqrt_d) / (2.0 * a);

	if((t1 < ERR_MARGIN && t2 < ERR_MARGIN) || (t1 > 1.0 && t2 > 1.0)) return 0;

	if(sp) {
		if(t1 < ERR_MARGIN) t1 = t2;
		if(t2 < ERR_MARGIN) t2 = t1;
		sp->dist = t1 < t2 ? t1 : t2;
		
		sp->pos.x = ray.orig.x + ray.dir.x * sp->dist;
		sp->pos.y = ray.orig.y + ray.dir.y * sp->dist;
		sp->pos.z = ray.orig.z + ray.dir.z * sp->dist;
		
		sp->normal.x = (sp->pos.x - sph->pos.x) / sph->rad;
		sp->normal.y = (sp->pos.y - sph->pos.y) / sph->rad;
		sp->normal.z = (sp->pos.z - sph->pos.z) / sph->rad;

		sp->vref = reflect(ray.dir, sp->normal);
		NORMALIZE(sp->vref);
	}
	return 1;
}

/* Load the scene from an extremely simple scene description file */
#define DELIM	" \t\n"
void load_scene(FILE *fp) {
	char line[256], *ptr, type;

	obj_list = malloc(sizeof(struct sphere));
	obj_list->next = 0;
	
	while((ptr = fgets(line, 256, fp))) {
		int i;
		struct vec3 pos, col;
		double rad, spow, refl;
		
		while(*ptr == ' ' || *ptr == '\t') ptr++;
		if(*ptr == '#' || *ptr == '\n') continue;

		if(!(ptr = strtok(line, DELIM))) continue;
		type = *ptr;
		
		for(i=0; i<3; i++) {
			if(!(ptr = strtok(0, DELIM))) break;
			*((double*)&pos.x + i) = atof(ptr);
		}

		if(type == 'l') {
			lights[lnum++] = pos;
			continue;
		}

		if(!(ptr = strtok(0, DELIM))) continue;
		rad = atof(ptr);

		for(i=0; i<3; i++) {
			if(!(ptr = strtok(0, DELIM))) break;
			*((double*)&col.x + i) = atof(ptr);
		}

		if(type == 'c') {
			cam.pos = pos;
			cam.targ = col;
			cam.fov = rad;
			continue;
		}

		if(!(ptr = strtok(0, DELIM))) continue;
		spow = atof(ptr);

		if(!(ptr = strtok(0, DELIM))) continue;
		refl = atof(ptr);

		if(type == 's') {
			struct sphere *sph = malloc(sizeof *sph);
			sph->next = obj_list->next;
			obj_list->next = sph;

			sph->pos = pos;
			sph->rad = rad;
			sph->mat.col = col;
			sph->mat.spow = spow;
			sph->mat.refl = refl;
		} else {
			fprintf(stderr, "unknown type: %c\n", type);
		}
	}
}


/* provide a millisecond-resolution timer for each system */
#if defined(__unix__) || defined(unix)
#include <time.h>
#include <sys/time.h>
unsigned long get_msec(void) {
	static struct timeval timeval, first_timeval;
	
	gettimeofday(&timeval, 0);
	if(first_timeval.tv_sec == 0) {
		first_timeval = timeval;
		return 0;
	}
	return (timeval.tv_sec - first_timeval.tv_sec) * 1000 + (timeval.tv_usec - first_timeval.tv_usec) / 1000;
}
#elif defined(__WIN32__) || defined(WIN32)
#include <windows.h>
unsigned long get_msec(void) {
	return GetTickCount();
}
#else
#error "I don't know how to measure time on your platform"
#endif
