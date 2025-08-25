#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "biomes.h"
#include "finders.h"
#include "generator.h"

// should be enough ig
#define MAX_PIECES 400
// as per mc wiki
#define REGIONSIZE 432

typedef unsigned long long ull;
typedef struct {
	Pos3 size;
	ull volume;
	Pos where;
} Result;
void result_set(Result *a, Result *b)
{
	a->size.x = b->size.x;
	a->size.y = b->size.y;
	a->size.z = b->size.z;
	a->volume = b->volume;
	a->where.x = b->where.x;
	a->where.z = b->where.z;
}

uint64_t seed;
Pos corners[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
int64_t start;
int64_t end;

Result biggest[21];

void setup(uint64_t real_seed)
{
	seed = real_seed & MASK48;
	start = 3750000 / REGIONSIZE + 1;
	end = 30000000 / REGIONSIZE + 1;
	for (int i = 0; i < 20; i++) {
		biggest[i].size.x = 0;
		biggest[i].size.y = 0;
		biggest[i].size.z = 0;
		biggest[i].volume = 0;
		biggest[i].where.x = 0;
		biggest[i].where.z = 0;
	}
}

void is_big(Pos3 *size, Pos *where)
{
	ull volume = size->x * size->y * size->z;
	int idx = 20;

	for (int i = 19; i >= 0; i--) {
		if (volume > biggest[i].volume) {
			idx--;
		} else {
			break;
		}
	}

	if (idx < 20) {
		for (int i = 19; i > idx; i--) {
			result_set(&biggest[i], &biggest[i - 1]);
		}
		biggest[idx].volume = volume;
		biggest[idx].size.x = size->x;
		biggest[idx].size.y = size->y;
		biggest[idx].size.z = size->z;
		biggest[idx].where.x = where->x;
		biggest[idx].where.z = where->z;
	}

	if (idx == 0) {
		printf("new biggest ! %dx%dx%d (%llu) at %d, %d\n", biggest[0].size.x,
		       biggest[0].size.y, biggest[0].size.z, biggest[0].volume,
		       biggest[0].where.x, biggest[0].where.z);
	}
}

void get_fortress_size(Pos3 *size, int mc, uint64_t seed, Pos *p)
{
	Pos3 bb0 = {40000000, 40000000, 40000000},
	     bb1 = {-40000000, -40000000, -40000000};
	Piece pieces[MAX_PIECES];
	int n =
	    getFortressPieces(pieces, MAX_PIECES, mc, seed, p->x >> 4, p->z >> 4);

	for (int i = 0; i < n; i++) {
		// FIXME IM ASSUMING SHIT

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
}

void do_it(size_t corner)
{
	int mc = MC_1_21;
	Generator g;

	setupGenerator(&g, mc, 0);
	applySeed(&g, DIM_NETHER, seed);

	for (int _x = start; _x < end; _x++) {
		for (int _z = start; _z < end; _z++) {
			int x = _x * corners[corner].x;
			int z = _z * corners[corner].z;
			Pos p;
			Pos3 size;

			if (!getStructurePos(Fortress, mc, seed, x, z, &p))
				continue;
			if (!isViableStructurePos(Fortress, &g, p.x, p.z, 0))
				continue;

			get_fortress_size(&size, mc, seed, &p);
			is_big(&size, &p);
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		puts("usage: bigfort <SEED>");
		return 1;
	}

	uint64_t real_seed = strtoull(argv[1], NULL, 0);

	setup(real_seed);
	for (size_t i = 0; i < 4; i++) {
		do_it(i);
	}
	do_it(0);

	puts("biggest 20 are:");
	for (int i = 0; i < 20; i++) {
		printf("%d. %dx%dx%d (%llu) at %d, %d\n", i, biggest[0].size.x,
		       biggest[0].size.y, biggest[0].size.z, biggest[0].volume,
		       biggest[0].where.x, biggest[0].where.z);
	}

	return 0;
}
