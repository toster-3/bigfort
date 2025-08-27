#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arg.h"
#include "biomes.h"
#include "finders.h"
#include "generator.h"

// should be enough ig
#define MAX_PIECES 400
// as per mc wiki
#define REGIONSIZE 432
#define BIGGEST_LEN 10

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
	bool counted[4];
	int checked;
	int pieces;
} BoundBox;

int n_threads = 24;
int64_t seed;
int64_t start = 3750000;
int64_t end = 30000000;
ull smallest[4];
Result biggest[4][BIGGEST_LEN];
Pos corners[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
char *names[4] = {"single", "double", "triple", "quad"};
char *argv0;
pthread_mutex_t printlock;
pthread_mutex_t sortlock[4];

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

void printf_s(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	pthread_mutex_lock(&printlock);
	vprintf(fmt, ap);
	pthread_mutex_unlock(&printlock);
	va_end(ap);
}

int cmp(const void *a, const void *b)
{
	Result *ra = (Result *)a;
	Result *rb = (Result *)b;
	return (rb->volume - ra->volume);
}

void setup(uint64_t real_seed)
{
	seed = real_seed & MASK48;
	start = start / REGIONSIZE + 1;
	end = end / REGIONSIZE;
	memset(biggest, 0, sizeof(biggest));
	memset(smallest, 0, sizeof(smallest));
	pthread_mutex_init(&printlock, NULL);
	for (int i = 0; i < 4; i++) {
		pthread_mutex_init(&sortlock[i], NULL);
	}
}

void strsize(char *s, Pos3 *size, int n_forts)
{
	char *curpos = s;
	for (int i = 0; i < n_forts; i++) {
		curpos += sprintf(curpos, "%dx%dx%d + ", size[i].x, size[i].y, size[i].z);
	}
	sprintf(curpos, "%dx%dx%d", size[n_forts].x, size[n_forts].y, size[n_forts].z);
}

ull bb_volume(BoundBox *b)
{
	if (b->end.x <= b->start.x || b->end.y <= b->start.y || b->end.z <= b->start.z)
		return 0;
	return (b->end.x - b->start.x) * (b->end.y - b->start.y) * (b->end.z - b->start.z);
}

BoundBox intersect(BoundBox *a, BoundBox *b)
{
	BoundBox ret = {0};

	ret.start.x = (a->start.x > b->start.x) ? a->start.x : b->start.x;
	ret.end.x = (a->end.x < b->end.x) ? a->end.x : b->end.x;
	ret.start.y = (a->start.y > b->start.y) ? a->start.y : b->start.y;
	ret.end.y = (a->end.y < b->end.y) ? a->end.y : b->end.y;
	ret.start.z = (a->start.z > b->start.z) ? a->start.z : b->start.z;
	ret.end.z = (a->end.z < b->end.z) ? a->end.z : b->end.z;

	return ret;
}

BoundBox intersect_n(BoundBox *bbs[4], int *idx, int n)
{
	BoundBox ret = *bbs[idx[0]];
	for (int i = 1; i < n; i++) {
		ret = intersect(&ret, bbs[idx[i]]);
	}
	return ret;
}

ull union_n(BoundBox *bbs[4], int n)
{
	ull total = 0, volume;
	BoundBox inter;

	for (int mask = 1; mask < (1 << n); mask++) {
		int idx[4] = {0}, k = 0;

		for (int i = 0; i < n; i++) {
			if (mask & (1 << i))
				idx[k++] = i;
		}

		inter = intersect_n(bbs, idx, k);
		volume = bb_volume(&inter);

		if (k % 2 == 1)
			total += volume;
		else
			total -= volume;
	}

	return total;
}

void is_big(BoundBox *bbs[4], int n_forts, Pos *pos, int tid)
{
	Pos3 size[4];
	char size_str[127];
	ull volume;

	for (int i = 0; i < n_forts + 1; i++) {
		if (bbs[i]->counted[n_forts]) {
			printf_s("hi hi hello\n");
			return;
		}
	}
	volume = union_n(bbs, n_forts + 1);
	for (int i = 0; i <= n_forts; i++) {
		size[i].x = bbs[i]->end.x - bbs[i]->start.x;
		size[i].y = bbs[i]->end.y - bbs[i]->start.y;
		size[i].z = bbs[i]->end.z - bbs[i]->start.z;
	}

	if (volume < smallest[n_forts])
		return;

	pthread_mutex_lock(&sortlock[n_forts]);

	if (volume > biggest[n_forts][BIGGEST_LEN - 1].volume) {
		for (int i = 0; i < n_forts; i++) {
			bbs[i]->counted[n_forts] = true;
			biggest[n_forts][BIGGEST_LEN - 1].size[i].x = size[i].x;
			biggest[n_forts][BIGGEST_LEN - 1].size[i].y = size[i].y;
			biggest[n_forts][BIGGEST_LEN - 1].size[i].z = size[i].z;
		}
		biggest[n_forts][BIGGEST_LEN - 1].volume = volume;
		biggest[n_forts][BIGGEST_LEN - 1].where.x = pos->x;
		biggest[n_forts][BIGGEST_LEN - 1].where.z = pos->z;
		qsort(biggest[n_forts], BIGGEST_LEN, sizeof(Result), cmp);
		smallest[n_forts] = biggest[n_forts][BIGGEST_LEN - 1].volume;
		strsize(size_str, size, n_forts);
		printf_s("new %s from thread %d ! %s (%llu) at %d %d\n", names[n_forts], tid, size_str, volume, pos->x, pos->z);
	}

	pthread_mutex_unlock(&sortlock[n_forts]);
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

int overlaps(BoundBox *a, BoundBox *b)
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
				if (overlaps(&buf[1][idx], &buf[1][idx + 1])) {
					bbs[n_forts++] = &buf[1][idx + 1];
				}
				where[0] = 1;
			} else if (fort_at(&buf[1][idx - 1], (_x - 1) * corners[data->corner].x, z, &g, NULL)) {
				if (overlaps(&buf[1][idx], &buf[1][idx - 1])) {
					bbs[n_forts++] = &buf[1][idx - 1];
				}
				where[0] = -1;
			}

			// Z
			if (fort_at(&buf[2][idx], x, (_z + 1) * corners[data->corner].z, &g, NULL)) {
				if (overlaps(&buf[1][idx], &buf[2][idx])) {
					bbs[n_forts++] = &buf[2][idx];
				}
				where[1] = 1;
			} else if (fort_at(&buf[0][idx], x, (_z - 1) * corners[data->corner].z, &g, NULL)) {
				if (overlaps(&buf[1][idx], &buf[0][idx])) {
					bbs[n_forts++] = &buf[0][idx];
				}
				where[1] = -1;
			}

			BoundBox fourth = buf[1 + where[0]][idx + where[1]];
			int fourth_x = (_x + where[0]) * corners[data->corner].x;
			int fourth_z = (_z + where[1]) * corners[data->corner].z;
			if (n_forts == 3 && fort_at(&fourth, fourth_x, fourth_z, &g, NULL) && overlaps(&buf[1][idx], &fourth) &&
			    overlaps(&buf[1 + where[0]][idx], &fourth) && overlaps(&buf[1][idx + where[0]], &fourth)) {
				bbs[n_forts++] = &fourth;
			}
			/*
			if (n_forts == 3 && fort_at(fourth, fourth_x, fourth_z, &g, NULL) && overlaps(&buf[1][idx], fourth)) {
			    bbs[n_forts++] = fourth;
			}
			*/

			is_big(bbs, n_forts - 1, &p, data->thread_id);
		}

		BoundBox *tmp = buf[0];
		buf[0] = buf[1];
		buf[1] = buf[2];
		buf[2] = tmp;
		memset(buf[2], 0, bufsize * sizeof(BoundBox));
	}

	printf_s("killing thread %d ...\n", data->thread_id);
	for (int i = 0; i < 3; i++) {
		free(buf[i]);
	}
	pthread_exit(NULL);
}

void usage(void)
{
	fprintf(stderr,
	        "Usage: %s -S <SEED>\n"
	        "Options:\n"
	        "\t-h | --help            : show this help message\n"
	        "\t-S | --seed <SEED>     : set the seed\n"
	        "\t-s | --start <START>   : set starting coordinate\n"
	        "\t-e | --end <END>       : set ending coordinate\n"
	        "\t-j | --jobs <THREADS>  : use <THREADS> threads, 24 by default\n"
	        "\t-c | --corner <CORNER> : start from <CORNER>, 1-4, this is useful if something crashes\n",
	        argv0);
	exit(0);
}

int main(int argc, char **argv)
{
	int can_i_do_shit = false, i = 0, chunk_size;
	uint64_t real_seed = 0;
	FILE *f;
	char size_str[127], buf[100];
	pthread_t *threads;
	ThreadData *tdata;

	LONG_ARGSTRUCT{{"help", 'h'}, {"seed", 'S'}, {"start", 's'}, {"end", 'e'}, {"jobs", 'j'}, {"corner", 'c'}};

	ELONG_ARGBEGIN(usage())
	{
	case 'h':
		usage();
		break;
	case 'S':
		can_i_do_shit = true;
		real_seed = strtoull(EARGF(usage()), NULL, 0);
		break;
	case 's':
		start = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'e':
		end = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'j':
		n_threads = strtol(EARGF(usage()), NULL, 0);
		if (n_threads < 1) {
			fprintf(stderr, "number of threads must be no smaller than 1\n");
			usage();
		}
		break;
	case 'c':
		i = strtol(EARGF(usage()), NULL, 0) - 1;
		if (i < 0 || i > 3) {
			fprintf(stderr, "corner must be in range 1-4\n");
			usage();
		}
	}
	ARGEND;

	if (can_i_do_shit == false) {
		usage();
	}

	threads = malloc(n_threads * sizeof(pthread_t));
	if (threads == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	tdata = malloc(n_threads * sizeof(ThreadData));
	if (tdata == NULL) {
		free(threads);
		fprintf(stderr, "out of memory\n");
		exit(2);
	}

	setup(real_seed);

	chunk_size = (end - start) / n_threads + 1;

	for (; i < 4; i++) {
		for (int t = 0; t < n_threads; t++) {
			tdata[t].thread_id = t;
			tdata[t].start = start + t * chunk_size;
			tdata[t].end = (t == n_threads - 1) ? end : start + (t + 1) * chunk_size;
			tdata[t].corner = i;

			int status;
			status = pthread_create(&threads[t], NULL, do_it, (void *)&tdata[t]);
			if (status != 0) {
				fprintf(stderr, "error creating thread %d\n", t);
				exit(-1);
			}
		}
		for (int t = 0; t < n_threads; t++) {
			pthread_join(threads[t], NULL);
		}
		printf("====================\n"
		       "!! corner %d done !!\n"
		       "====================\n",
		       i + 1);
		switch (i) {
		case 0:
			f = fopen("results_1.txt", "w");
			break;
		case 1:
			f = fopen("results_2.txt", "w");
			break;
		case 2:
			f = fopen("results_3.txt", "w");
			break;
		default:
			f = stdout;
		}
		if (i < 3) {
			for (int i = 0; i < 4; i++) {
				fprintf(f, "%ss:\n", names[i]);
				for (int j = 0; j < BIGGEST_LEN; j++) {
					strsize(size_str, biggest[i][j].size, i);
					fprintf(f, "%d. %s (%llu) at %d %d\n", j + 1, size_str, biggest[i][j].volume, biggest[i][j].where.x,
					        biggest[i][j].where.z);
				}
			}

			fclose(f);
		}
	}

	for (i = 0; i < 4; i++) {
		qsort(biggest[i], BIGGEST_LEN, sizeof(Result), cmp);
	}
	f = fopen("results.txt", "w");
	if (f == NULL) {
		f = stdout;
	}

	for (i = 0; i < 4; i++) {
		fprintf(f, "%ss:\n", names[i]);
		for (int j = 0; j < BIGGEST_LEN; j++) {
			strsize(size_str, biggest[i][j].size, i);
			fprintf(f, "%d. %s (%llu) at %d %d\n", j + 1, size_str, biggest[i][j].volume, biggest[i][j].where.x,
			        biggest[i][j].where.z);
		}
	}

	if (f == stdout)
		goto cleanup;

	fclose(f);

	f = fopen("results.txt", "r");
	if (f == NULL) {
		return 1;
	}

	while (fgets(buf, sizeof(buf), f) != NULL) {
		printf("%s", buf);
	}

cleanup:
	free(threads);
	free(tdata);

	return 0;
}
