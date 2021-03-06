/*
 * Copyright 2011 Vincent Sanders <vince@kyllikki.org>
 *
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * This file is part of png23d. 
 * 
 * mesh generation routines header.
 */

#ifndef PNG23D_MESH_GEN_H
#define PNG23D_MESH_GEN_H 1

/** Convert raster image into triangle mesh
 *
 */
bool mesh_from_bitmap(struct mesh *mesh, bitmap *bm, options *options);

#endif
