/*
 * Copyright 2011 Vincent Sanders <vince@kyllikki.org>
 *
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * This file is part of png23d.
 *
 * Routines to handle mesh vertex list construction.
 *
 * The bloom implementation found here draws inspiration from several sources
 * including:
 *
 * - Simon Howard C algorithms (licenced under ISC licence)
 * - Lars Wirzenius publib (which he said I could licence as I saw fit ;-)
 * - The paper "Hash Function for Triangular Mesh Reconstruction" by Václav
 *       Skala, Jan Hrádek, Martin Kuchař (Department of Computer Science and
 *       Engineering, University of West Bohemia)
 *
 * These served as sources of code snippets and algorihms but none of them are
 * responsible for this implementation which is my fault.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "option.h"
#include "bitmap.h"
#include "mesh.h"
#include "mesh_gen.h"
#include "mesh_bloom.h"


/* Salt values.  These salts are XORed with the output of the hash function to
 * give multiple unique hashes.
 *
 * These are "nothing up my sleeve" numbers: they are derived from the first
 * 256 numbers in the book "A Million Random Digits with 100,000 Normal
 * Deviates" published by the RAND corporation, ISBN 0-8330-3047-7.
 *
 * The numbers here were derived by taking each number from the book in turn,
 * then multiplying by 256 and dividing by 100,000 to give a byte range value.
 * Groups of four numbers were then combined to give 32-bit integers, most
 * significant byte first.
 */

static const unsigned int salts[] = {
    0x1953c322, 0x588ccf17, 0x64bf600c, 0xa6be3f3d,
    0x341a02ea, 0x15b03217, 0x3b062858, 0x5956fd06,
    0x18b5624f, 0xe3be0b46, 0x20ffcd5c, 0xa35dfd2b,
    0x1fc4a9bf, 0x57c45d5c, 0xa8661c4a, 0x4f1b74d2,
    0x5a6dde13, 0x3b18dac6, 0x05a8afbf, 0xbbda2fe2,
    0xa2520d78, 0xe7934849, 0xd541bc75, 0x09a55b57,
    0x9b345ae2, 0xfc2d26af, 0x38679cef, 0x81bd1e0d,
    0x654681ae, 0x4b3d87ad, 0xd5ff10fb, 0x23b32f67,
    0xafc7e366, 0xdd955ead, 0xe7c34b1c, 0xfeace0a6,
    0xeb16f09d, 0x3c57a72d, 0x2c8294c5, 0xba92662a,
    0xcd5b2d14, 0x743936c8, 0x2489beff, 0xc6c56e00,
    0x74a4f606, 0xb244a94a, 0x5edfc423, 0xf1901934,
    0x24af7691, 0xf6c98b25, 0xea25af46, 0x76d5f2e6,
    0x5e33cdf2, 0x445eb357, 0x88556bd2, 0x70d1da7a,
    0x54449368, 0x381020bc, 0x1c0520bf, 0xf7e44942,
    0xa27e2a58, 0x66866fc5, 0x12519ce7, 0x437a8456,
};

bool
mesh_bloom_init(struct mesh *mesh,
                unsigned int entries,
                unsigned int iterations)
{
    /* The salt table size imposes a limit on the number of iterations which
     * can be applied
     */

    if (iterations > sizeof(salts) / sizeof(*salts)) {
        return false;
    }

    /* Allocate table, each entry is one bit, packed into bytes. */

    mesh->bloom_table = calloc(1, (entries + 7) / 8);

    if (mesh->bloom_table == NULL) {
        return false;
    }

    mesh->bloom_iterations = iterations;
    mesh->bloom_table_entries = entries;

    return true;
}

/*
(int) α * ((int) (abs(X) * Q)) / Q +
      β * ((int) (abs(Y) * Q)) / Q +
      γ * ((int) (abs(Z) * Q)) / Q

(int) is the conversion to integer - the fractional part of the float is
removed (in current implementation DWORD is used),

α,β,γ are coefficients of hash function (originally used 3, 5 and 7),

Q defines sensitivity (numerical error elimination) - for 3 decimal digits set
Q = 1000.0,


 */
static inline unsigned int
mesh_bloom_hash(struct pnt *pnt)
{
    unsigned int hash;
    hash = (3 * ((unsigned int)(abs(pnt->x) * 1000.0))) / 100;
    hash += (257 * ((unsigned int)(abs(pnt->y) * 1000.0)) / 100);
    hash += (653 * ((unsigned int)(abs(pnt->z) * 1000.0)) / 100);
    return hash;
}

static void
mesh_bloom_insert(struct mesh *mesh, struct pnt *pnt)
{
    unsigned int hash;
    unsigned int subhash;
    unsigned int index;
    unsigned int iloop; /* iteration loop */
    uint8_t b;

    /* Generate hash of the point to insert */
    hash = mesh_bloom_hash(pnt);

    /* Generate multiple unique hashes by XORing with values in the
     * salt table.
     */

    for (iloop = 0; iloop < mesh->bloom_iterations; ++iloop) {

        /* Generate a unique hash */
        subhash = hash ^ salts[iloop];

        /* Find the index into the table */
        index = subhash % mesh->bloom_table_entries;

        /* Insert into the table.
         * index / 8 finds the byte index of the table,
         * index % 8 gives the bit index within that byte to set. */

        b = (uint8_t) (1 << (index % 8));
        mesh->bloom_table[index / 8] |= b;
    }
}

static bool
mesh_bloom_query(struct mesh *mesh, struct pnt *pnt)
{
    unsigned int hash;
    unsigned int subhash;
    unsigned int index;
    unsigned int iloop;
    unsigned char b;
    int bit;

    /* Generate hash of the value to lookup */

    hash = mesh_bloom_hash(pnt);

    /* Generate multiple unique hashes by XORing with values in the
     * salt table. */

    for (iloop = 0; iloop < mesh->bloom_iterations; ++iloop) {

        /* Generate a unique hash */

        subhash = hash ^ salts[iloop];

        /* Find the index into the table to test */

        index = subhash % mesh->bloom_table_entries;

        /* The byte at index / 8 holds the value to test */

        b = mesh->bloom_table[index / 8];
        bit = 1 << (index % 8);

        /* Test if the particular bit is set; if it is not set,
         * this value can not have been inserted. */

        if ((b & bit) == 0) {
            return false;
        }
    }

    /* All necessary bits were set.  This may indicate that the value
     * was inserted, or the values could have been set through other
     * insertions. */

    return true;
}


static inline uint32_t
find_pnt(struct mesh *mesh, struct pnt *pnt)
{
    uint32_t idx;


    for (idx = 0; idx < mesh->pcount; idx++) {
        if ((mesh->p[idx].pnt.x == pnt->x) &&
            (mesh->p[idx].pnt.y == pnt->y) &&
            (mesh->p[idx].pnt.z == pnt->z)) {
            break;
        }

    }

    mesh->find_count++;
    mesh->find_cost+=idx;

    return idx;
}

idxpnt
mesh_add_pnt(struct mesh *mesh, struct pnt *npnt)
{
    uint32_t idx;
    bool in_bloom;

    in_bloom = mesh_bloom_query(mesh, npnt);

    if (in_bloom == false) {
        idx = mesh->pcount; /* not already in list */
    } else {
        idx = find_pnt(mesh, npnt);

        if (idx == mesh->pcount) {
            /* seems the bloom failed to filter this one */
            mesh->bloom_miss++;
        }
    }

    if (idx == mesh->pcount) {
        /* not in array already */
        if ((mesh->pcount + 1) > mesh->palloc) {
            /* pnt array needs extending */
            mesh->p = realloc(mesh->p,
                              (mesh->palloc + 1000) *
                              sizeof(struct vertex));
            mesh->palloc += 1000;
        }

        mesh_bloom_insert(mesh, npnt);
        mesh->p[mesh->pcount].pnt = *npnt;
        mesh->p[mesh->pcount].fcount = 0;
        mesh->pcount++;
    }
    return idx;
}