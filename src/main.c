#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>

#include "biomes.h"
#include "finders.h"
#include "generator.h"

// should be enough ig
#define MAX_PIECES 400
// as per mc wiki
#define REGIONSIZE 432
#define NUM_THREADS 24

typedef unsigned long long ull;
typedef struct {
	Pos3 size[4];
	Pos where;
	ull volume;
} Result;

typedef struct {
	int corner;
	int thread_id;
	int start;
	int end;
} ThreadData;

typedef struct {
	Pos3 start;
	Pos3 end;
	int checked;
	int pieces;
} BoundBox;

void result_set(Result *a, Result *b)
{
	for (int i = 0; i < 4; i++) {
		a->size[i].x = b->size[i].x;
		a->size[i].y = b->size[i].y;
		a->size[i].z = b->size[i].z;
	}
	a->volume = b->volume;
	a->where.x = b->where.x;
	a->where.z = b->where.z;
}

uint64_t seed;
Pos corners[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
char *names[4] = {"single", "double", "triple", "QUAD QUAD QUAD QUAD"};
int64_t start;
int64_t end;

pthread_mutex_t printlock;

Result biggest[4][NUM_THREADS];

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
	start = 3750000 / REGIONSIZE + 1;
	end = 30000000 / REGIONSIZE;
	// start = 1000 / REGIONSIZE + 1;
	// end = 100000 / REGIONSIZE;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < NUM_THREADS; i++) {
			for (int k = 0; k < 4; k++) {
				biggest[j][i].size[k].x = 0;
				biggest[j][i].size[k].y = 0;
				biggest[j][i].size[k].z = 0;
			}
			biggest[j][i].volume = 0;
			biggest[j][i].where.x = 0;
			biggest[j][i].where.z = 0;
		}
	}
	pthread_mutex_init(&printlock, NULL);
}

void strsize(char *s, Pos3 *size, int n_forts)
{
	char *curpos = s;
	for (int i = 0; i < n_forts; i++) {
		curpos += sprintf(curpos, "%dx%dx%d + ", size[i].x, size[i].y, size[i].z);
	}
	sprintf(curpos, "%dx%dx%d", size[n_forts].x, size[n_forts].y, size[n_forts].z);
}

void is_big(BoundBox *bbs[4], int n_forts, Pos *pos, int tid)
{
	Pos3 size[4];
	char size_str[127];
	ull volume = 0;
	for (int i = 0; i <= n_forts; i++) {
		size[i].x = bbs[i]->end.x - bbs[i]->start.x;
		size[i].y = bbs[i]->end.y - bbs[i]->start.y;
		size[i].z = bbs[i]->end.z - bbs[i]->start.z;
		volume += size[i].x * size[i].y * size[i].z;
	}

	if (volume > biggest[n_forts][tid].volume) {
		for (int i = 0; i <= n_forts; i++) {
			biggest[n_forts][tid].size[i].x = size[i].x;
			biggest[n_forts][tid].size[i].y = size[i].y;
			biggest[n_forts][tid].size[i].z = size[i].z;
		}
		biggest[n_forts][tid].volume = volume;
		biggest[n_forts][tid].where.x = pos->x;
		biggest[n_forts][tid].where.z = pos->z;

		strsize(size_str, size, n_forts);
		printf_s("new biggest %s in thread %d ! %s (%llu) at %d %d\n", names[n_forts], tid, size_str, volume, pos->x,
		         pos->z);
	}
}

void get_fortress_bb(BoundBox *bb, int mc, uint64_t seed, Pos *p)
{
	Pos3 bb0 = {40000000, 40000000, 40000000}, bb1 = {-40000000, -40000000, -40000000};
	Piece pieces[MAX_PIECES];
	int n = getFortressPieces(pieces, MAX_PIECES, mc, seed, p->x >> 4, p->z >> 4);

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

	bb->start = bb0;
	bb->end = bb1;
	bb->pieces = n;
}

inline void size_from_bb(Pos3 *size, BoundBox *bb)
{
	size->x = bb->end.x - bb->start.x;
	size->y = bb->end.y - bb->start.y;
	size->z = bb->end.z - bb->start.z;
}

ull pos3volume(Pos3 *size)
{
	return size->x * size->y * size->z;
}

int fort_at(BoundBox *bb, int x, int z, Generator *g, Pos *p)
{
	Pos q;
	if (bb->checked) {
		if (bb->pieces) {
			q.x = bb->start.x;
			q.z = bb->start.z;
			goto set_pos;
		} else {
			return 0;
		}
	} else {
		bb->checked = 1;
		if (!getStructurePos(Fortress, MC_1_21, seed, x, z, &q))
			return 0;
		if (!isViableStructurePos(Fortress, g, q.x, q.z, 0))
			return 0;
	}
	get_fortress_bb(bb, MC_1_21, seed, &q);

set_pos:
	if (p != NULL) {
		p->x = q.x;
		p->z = q.z;
	}
	return 1;
}

inline ull bb_volume(BoundBox *bb)
{
	return (bb->end.x - bb->start.x) * (bb->end.y - bb->start.y) * (bb->end.z - bb->start.z);
}

int intersect(BoundBox *a, BoundBox *b)
{
	int collision = 0;

	if (a->start.x < b->end.x && a->end.x > b->start.x && a->start.y < b->end.y && a->end.y > b->start.y &&
	    a->start.z < b->end.z && a->end.z > b->start.z)
		collision = 1;

	return collision;
}

void *do_it(void *arg)
{
	ThreadData *data = (ThreadData *)arg;
	int mc = MC_1_21;
	Generator g;
	int bufsize = data->end - data->start + 3;
	int _x, _z;
	BoundBox *buf[3], *bbs[4];
	for (int i = 0; i < 3; i++) {
		buf[i] = calloc(bufsize, sizeof(BoundBox));
		if (!buf[i]) {
			fprintf(stderr, "error: thread %d out of memory\n", data->thread_id);
			exit(2);
		}
	}

	printf_s("hello i am thread %d i do %d to %d\n", data->thread_id, data->start, data->end);
	setupGenerator(&g, mc, 0);
	applySeed(&g, DIM_NETHER, seed);

	for (_z = start; _z <= end; _z++) {
		for (_x = data->start; _x < data->end; _x++) {
			int x = _x * corners[data->corner].x;
			int z = _z * corners[data->corner].z;
			int idx = _x - data->start + 1;
			int n_forts = 0, where[2] = {0};
			Pos p;

			if (!fort_at(&buf[1][idx], x, z, &g, &p)) {
				continue;
			}
			bbs[n_forts++] = &buf[1][idx];

			// X
			if (fort_at(&buf[1][idx + 1], (_x + 1) * corners[data->corner].x, z, &g, NULL)) {
				if (intersect(&buf[1][idx], &buf[1][idx + 1])) {
					bbs[n_forts++] = &buf[1][idx + 1];
				}
				where[0] = 1;
			} else if (fort_at(&buf[1][idx - 1], (_x - 1) * corners[data->corner].x, z, &g, NULL)) {
				if (intersect(&buf[1][idx], &buf[1][idx - 1])) {
					bbs[n_forts++] = &buf[1][idx - 1];
				}
				where[0] = -1;
			}

			// Z
			if (fort_at(&buf[2][idx], x, (_z + 1) * corners[data->corner].z, &g, NULL)) {
				if (intersect(&buf[1][idx], &buf[2][idx])) {
					bbs[n_forts++] = &buf[2][idx];
				}
				where[1] = 1;
			} else if (fort_at(&buf[0][idx], x, (_z - 1) * corners[data->corner].z, &g, NULL)) {
				if (intersect(&buf[1][idx], &buf[0][idx])) {
					bbs[n_forts++] = &buf[0][idx];
				}
				where[1] = -1;
			}

			BoundBox *fourth = &buf[1 + where[0]][idx + where[1]];
			int fourth_x = (_x + where[0]) * corners[data->corner].x;
			int fourth_z = (_z + where[1]) * corners[data->corner].z;
			if (n_forts == 3 && fort_at(fourth, fourth_x, fourth_z, &g, NULL) && intersect(&buf[1][idx], fourth) &&
			    intersect(&buf[1 + where[0]][idx], fourth) && intersect(&buf[1][idx + where[0]], fourth)) {
				bbs[n_forts++] = fourth;
			}
			/*
			if (n_forts == 3 && fort_at(fourth, fourth_x, fourth_z, &g, NULL) && intersect(&buf[1][idx], fourth)) {
			    bbs[n_forts++] = fourth;
			}
			*/

			is_big(bbs, n_forts - 1, &p, data->thread_id);
		}

		BoundBox *tmp = buf[0];
		buf[0] = buf[1];
		buf[1] = buf[2];
		buf[2] = tmp;
		for (int i = 0; i < bufsize; i++) {
			buf[2][i].checked = 0;
			buf[2][i].pieces = 0;
			buf[2][i].start.x = 0;
			buf[2][i].start.y = 0;
			buf[2][i].start.z = 0;
			buf[2][i].end.x = 0;
			buf[2][i].end.y = 0;
			buf[2][i].end.z = 0;
		}
	}

	printf_s("killing thread %d ...\n", data->thread_id);
	for (int i = 0; i < 3; i++) {
		free(buf[i]);
	}
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
			tdata[t].end = (t == NUM_THREADS - 1) ? end : start + (t + 1) * chunk_size;
			tdata[t].corner = i;

			int status;
			status = pthread_create(&threads[t], NULL, do_it, (void *)&tdata[t]);
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
		       i + 1);
	}

	for (int i = 0; i < 4; i++) {
		qsort(biggest[i], NUM_THREADS, sizeof(Result), cmp);
	}
	char size_str[127];
	FILE *f = fopen("results.txt", "w");
	if (f == NULL) {
		f = stdout;
	}

	for (int i = 0; i < 4; i++) {
		fprintf(f, "%ss:\n", names[i]);
		for (int j = 0; j < NUM_THREADS; j++) {
			strsize(size_str, biggest[i][j].size, i);
			fprintf(f, "%d. %s (%llu) at %d %d\n", j + 1, size_str, biggest[i][j].volume, biggest[i][j].where.x,
			        biggest[i][j].where.z);
		}
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
