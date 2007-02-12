/*
  Copyright (c) 2006 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2006 Center for Bioinformatics, University of Hamburg
  See LICENSE file or http://genometools.org/license.html for license details.
*/

#ifndef CSA_VISITOR_H
#define CSA_VISITOR_H

/* implements the ``genome visitor'' interface */
typedef struct Csa_visitor Csa_visitor;

#include "genome_visitor.h"

const GenomeVisitorClass* csa_visitor_class(void);
GenomeVisitor*             csa_visitor_new(unsigned long join_length);
unsigned long               csa_visitor_node_buffer_size(GenomeVisitor*);
GenomeNode*                csa_visitor_get_node(GenomeVisitor*);
void                        csa_visitor_process_cluster(GenomeVisitor*,
                                                        unsigned int
                                                        final_cluster, Log*);

#endif
