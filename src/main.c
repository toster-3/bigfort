#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "biomes.h"
#include "finders.h"
#include "generator.h"

// should be enough ig
#define MAX_PIECES 400
// as per mc wiki
#define REGIONSIZE 432
#define NUM_THREADS 64

typedef unsigned long long ull;
typedef struct {
	Pos3 size;
	Pos where;
	ull volume;
	int n_pieces;
} Result;

typedef struct {
	int corner;
	int thread_id;
	int start;
	int end;
} ThreadData;

void result_set(Result *a, Result *b)
{
	a->size.x = b->size.x;
	a->size.y = b->size.y;
	a->size.z = b->size.z;
	a->volume = b->volume;
	a->where.x = b->where.x;
	a->where.z = b->where.z;
	a->n_pieces = b->n_pieces;
}

uint64_t seed;
Pos corners[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
int64_t start;
int64_t end;

pthread_mutex_t printlock;

Result biggest[NUM_THREADS];

void printf_s(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	pthread_mutex_lock(&printlock);
	vprintf(fmt, ap);
	pthread_mutex_unlock(&printlock);
	va_end(ap);
}

void setup(uint64_t real_seed)
{
	seed = real_seed & MASK48;
	// start = 3750000 / REGIONSIZE + 1;
	// end = 30000000 / REGIONSIZE;
	start = 10000 / REGIONSIZE + 1;
	end = 1000000 / REGIONSIZE;
	for (int i = 0; i < 20; i++) {
		biggest[i].size.x = 0;
		biggest[i].size.y = 0;
		biggest[i].size.z = 0;
		biggest[i].volume = 0;
		biggest[i].where.x = 0;
		biggest[i].where.z = 0;
		biggest[i].n_pieces = 0;
	}
	pthread_mutex_init(&printlock, NULL);
}

void is_big(Pos3 *size, Pos *where, int n_pieces, int tid)
{
	ull volume = size->x * size->y * size->z;

	if (volume > biggest[tid].volume) {
		biggest[tid].volume = volume;
		biggest[tid].size.x = size->x;
		biggest[tid].size.y = size->y;
		biggest[tid].size.z = size->z;
		biggest[tid].where.x = where->x;
		biggest[tid].where.z = where->z;
		biggest[tid].n_pieces = n_pieces;
		printf_s("new biggest in thread %d ! %dx%dx%d (%llu) at %d, %d with %d "
		         "pieces\n",
		         tid, biggest[tid].size.x, biggest[tid].size.y,
		         biggest[tid].size.z, biggest[tid].volume, biggest[tid].where.x,
		         biggest[tid].where.z, biggest[tid].n_pieces);
	}
}

int get_fortress_size(Pos3 *size, int mc, uint64_t seed, Pos *p)
{
	Pos3 bb0 = {40000000, 40000000, 40000000},
	     bb1 = {-40000000, -40000000, -40000000};
	Piece pieces[MAX_PIECES];
	int n =
	    getFortressPieces(pieces, MAX_PIECES, mc, seed, p->x >> 4, p->z >> 4);

	for (int i = 0; i < n; i++) {
		if (pieces[i].bb0.x < bb0.x) {
			bb0.x = pieces[i].bb0.x;
		}
		if (pieces[i].bb0.y < bb0.y) {
			bb0.y = pieces[i].bb0.y;
		}
		if (pieces[i].bb0.z < bb0.z) {
			bb0.z = pieces[i].bb0.z;
		}
		if (pieces[i].bb1.x > bb1.x) {
			bb1.x = pieces[i].bb1.x;
		}
		if (pieces[i].bb1.y > bb1.y) {
			bb1.y = pieces[i].bb1.y;
		}
		if (pieces[i].bb1.z > bb1.z) {
			bb1.z = pieces[i].bb1.z;
		}
	}

	size->x = bb1.x - bb0.x;
	size->y = bb1.y - bb0.y;
	size->z = bb1.z - bb0.z;

	return n;
}

void *do_it(void *arg)
{
	ThreadData *data = (ThreadData *)arg;
	int mc = MC_1_21;
	Generator g;
	printf_s("hello i am thread %d i do %d to %d\n", data->thread_id,
	         data->start, data->end);

	setupGenerator(&g, mc, 0);
	applySeed(&g, DIM_NETHER, seed);

	for (int _x = data->start; _x < data->end; _x++) {
		for (int _z = start; _z < end; _z++) {
			int x = _x * corners[data->corner].x;
			int z = _z * corners[data->corner].z;
			Pos p;
			Pos3 size;

			if (!getStructurePos(Fortress, mc, seed, x, z, &p))
				continue;
			if (!isViableStructurePos(Fortress, &g, p.x, p.z, 0))
				continue;

			int n = get_fortress_size(&size, mc, seed, &p);
			is_big(&size, &p, n, data->thread_id);
		}
	}

	printf_s("killing thread %d ...\n", data->thread_id);
	pthread_exit(NULL);
}

int cmp(const void *a, const void *b)
{
	Result *ra = (Result *)a;
	Result *rb = (Result *)b;
	return (rb->volume - ra->volume);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		puts("usage: bigfort <SEED>");
		return 1;
	}

	uint64_t real_seed = strtoull(argv[1], NULL, 0);
	pthread_t threads[NUM_THREADS];
	ThreadData tdata[NUM_THREADS];

	setup(real_seed);

	int chunk_size = (end - start) / NUM_THREADS + 1;

	for (int i = 0; i < 4; i++) {
		for (int t = 0; t < NUM_THREADS; t++) {
			tdata[t].thread_id = t;
			tdata[t].start = start + t * chunk_size;
			tdata[t].end =
			    (t == NUM_THREADS - 1) ? end : start + (t + 1) * chunk_size;
			tdata[t].corner = i;

			int status;
			status =
			    pthread_create(&threads[t], NULL, do_it, (void *)&tdata[t]);
			if (status != 0) {
				fprintf(stderr, "error creating thread %d\n", t);
				exit(-1);
			}
		}
		for (int t = 0; t < NUM_THREADS; t++) {
			pthread_join(threads[t], NULL);
		}
		printf("====================\n"
		       "!! corner %d done !!\n"
		       "====================\n",
		       i);
	}

	qsort(biggest, NUM_THREADS, sizeof(Result), cmp);

	FILE *f = fopen("results.txt", "w");
	if (f == NULL) {
		f = stdout;
	}

	fputs("biggest are:\n", f);
	for (int i = 0; i < NUM_THREADS; i++) {
		fprintf(f, "%d. %dx%dx%d (%llu) at %d, %d with %d pieces\n", i + 1,
		        biggest[i].size.x, biggest[i].size.y, biggest[i].size.z,
		        biggest[i].volume, biggest[i].where.x, biggest[i].where.z,
		        biggest[i].n_pieces);
	}

	if (f == stdout)
		return 0;

	fclose(f);

	f = fopen("results.txt", "r");
	if (f == NULL) {
		return 1;
	}

	char buf[100];
	while (fgets(buf, sizeof(buf), f) != NULL) {
		printf("%s", buf);
	}

	return 0;
}
