/*
 *  Copyright (C) 2004  The revenge Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * 11-9-2004 Initial work.
 *   Copyright (C) 2004 James Courtier-Dutton James@superbug.co.uk
 * 10-11-2007 Updates.
 *   Copyright (C) 2007 James Courtier-Dutton James@superbug.co.uk
 * 29-03-2009 Updates.
 *   Copyright (C) 2009 James Courtier-Dutton James@superbug.co.uk
 */

/* Intel ia32 instruction format: -
 Instruction-Prefixes (Up to four prefixes of 1-byte each. [optional] )
 Opcode (1-, 2-, or 3-byte opcode)
 ModR/M (1 byte [if required] )
 SIB (Scale-Index-Base:1 byte [if required] )
 Displacement (Address displacement of 1, 2, or 4 bytes or none)
 Immediate (Immediate data of 1, 2, or 4 bytes or none)

 Naming convention taked from Intel Instruction set manual,
 Appendix A. 25366713.pdf
*/

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <rev.h>

#define EIP_START 0x40000000

struct dis_instructions_s dis_instructions;
uint8_t *inst;
size_t inst_size = 0;
uint8_t *data;
size_t data_size = 0;
struct rev_eng *handle;
struct disassemble_info disasm_info;
char *dis_flags_table[] = { " ", "f" };
uint64_t inst_log = 1;	/* Pointer to the current free instruction log entry. */
int local_counter = 0x100;
struct self_s *self = NULL;

char *condition_table[] = {
	"OVERFLOW_0", /* Signed */
	"NOT_OVERFLOW_1", /* Signed */
	"BELOW_2",	/* Unsigned */
	"NOT_BELOW_3",	/* Unsigned */
	"EQUAL_4",	/* Signed or Unsigned */
	"NOT_EQUAL_5",	/* Signed or Unsigned */
	"BELOW_EQUAL_6",	/* Unsigned */
	"ABOVE_7",	/* Unsigned */
	"SIGNED_8",	/* Signed */
	"NO_SIGNED_9",	/* Signed */
	"PARITY_10",	/* Signed or Unsigned */
	"NOT_PARITY_11",/* Signed or Unsigned */
	"LESS_12",	/* Signed */
	"GREATER_EQUAL_13", /* Signed */
	"LESS_EQUAL_14",    /* Signed */
	"GREATER_15"	/* Signed */
};

struct relocation_s {
	int type; /* 0 = invalid, 1 = external_entry_point, 2 = data */
	uint64_t index; /* Index into the external_entry_point or data */
};

struct mid_start_s {
	uint64_t mid_start;
	uint64_t valid;
};

/* Params order:
 * int test30(int64_t param_reg0040, int64_t param_reg0038, int64_t param_reg0018, int64_t param_reg0010, int64_t param_reg0050, int64_t param_reg0058, int64_t param_stack0008, int64_t param_stack0010)
 */

/* Used to store details of each instruction.
 * Linked by prev/next pointers
 * so that a single list can store all program flow.
 */
// struct inst_log_entry_s inst_log_entry[INST_LOG_ENTRY_SIZE];
int search_back_seen[INST_LOG_ENTRY_SIZE];

/* Used to keep record of where we have been before.
 * Used to identify program flow, branches, and joins.
 */
int memory_used[MEMORY_USED_SIZE];
/* Used to keep a non bfd version of the relocation entries */
int memory_relocation[MEMORY_USED_SIZE];

/* This is used to hold return values from process block */
struct entry_point_s entry_point[ENTRY_POINTS_SIZE];
uint64_t entry_point_list_length = ENTRY_POINTS_SIZE;

int print_dis_instructions(struct self_s *self)
{
	int n;
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;

	printf("print_dis_instructions:\n");
	for (n = 1; n <= inst_log; n++) {
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		if (print_inst(self, instruction, n, NULL))
			return 1;
		printf("start_address:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.start_address,
			inst_log1->value2.start_address,
			inst_log1->value3.start_address);
		printf("init:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.init_value,
			inst_log1->value2.init_value,
			inst_log1->value3.init_value);
		printf("offset:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.offset_value,
			inst_log1->value2.offset_value,
			inst_log1->value3.offset_value);
		printf("indirect init:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_init_value,
			inst_log1->value2.indirect_init_value,
			inst_log1->value3.indirect_init_value);
		printf("indirect offset:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_offset_value,
			inst_log1->value2.indirect_offset_value,
			inst_log1->value3.indirect_offset_value);
		printf("indirect value_id:%"PRIx64", %"PRIx64" -> %"PRIx64"\n",
			inst_log1->value1.indirect_value_id,
			inst_log1->value2.indirect_value_id,
			inst_log1->value3.indirect_value_id);
		printf("value_type:0x%x, 0x%x -> 0x%x\n",
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		printf("value_scope:0x%x, 0x%x -> 0x%x\n",
			inst_log1->value1.value_scope,
			inst_log1->value2.value_scope,
			inst_log1->value3.value_scope);
		printf("value_id:0x%"PRIx64", 0x%"PRIx64" -> 0x%"PRIx64"\n",
			inst_log1->value1.value_id,
			inst_log1->value2.value_id,
			inst_log1->value3.value_id);
		if (inst_log1->prev_size > 0) {
			int n;
			for (n = 0; n < inst_log1->prev_size; n++) {
				printf("inst_prev:%d:0x%04x\n",
					n,
					inst_log1->prev[n]);
			}
		}
		if (inst_log1->next_size > 0) {
			int n;
			for (n = 0; n < inst_log1->next_size; n++) {
				printf("inst_next:%d:0x%04x\n",
					n,
					inst_log1->next[n]);
			}
		}
	}
	return 0;
}

/* This scans for duplicates in the inst_log1->next[] and inst_log1->prev[n] lists. */
int tidy_inst_log(struct self_s *self)
{
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;
	int l,m,n;

	for (n = 1; n <= inst_log; n++) {
		inst_log1 =  &inst_log_entry[n];
		if (inst_log1->next_size > 1) {
			for (m = 0; m < (inst_log1->next_size - 1); m++) {
				for (l = m + 1; l < inst_log1->next_size; l++) {
					printf("next: 0x%x: m=0x%x, l=0x%x\n", n, inst_log1->next[m],inst_log1->next[l]);
					if (inst_log1->next[m] == inst_log1->next[l]) {
						inst_log1->next[m] = 0;
					}
				}
			}
			for (m = 0; m < (inst_log1->next_size - 1); m++) {
				if (inst_log1->next[m] == 0) {
					for (l = m + 1; l < (inst_log1->next_size); l++) {
						inst_log1->next[l - 1] = inst_log1->next[l];
					}
					inst_log1->next_size--;
					inst_log1->next = realloc(inst_log1->next, inst_log1->next_size * sizeof(int));
				}
			}
		}
		if (inst_log1->prev_size > 1) {
			for (m = 0; m < (inst_log1->prev_size - 1); m++) {
				for (l = m + 1; l < inst_log1->prev_size; l++) {
					printf("prev: 0x%x: m=0x%x, l=0x%x\n", n, inst_log1->prev[m],inst_log1->prev[l]);
					if (inst_log1->prev[m] == inst_log1->prev[l]) {
						inst_log1->prev[m] = 0;
					}
				}
			}
			for (m = 0; m < (inst_log1->prev_size - 1); m++) {
				if (inst_log1->prev[m] == 0) {
					for (l = m + 1; l < (inst_log1->prev_size); l++) {
						inst_log1->prev[l - 1] = inst_log1->prev[l];
					}
					inst_log1->prev_size--;
					inst_log1->prev = realloc(inst_log1->prev, inst_log1->prev_size * sizeof(int));
				}
			}
		}

	}
	return 0;
}

int find_node_from_inst(struct self_s *self, struct control_flow_node_s *nodes, int *node_size, int inst)
{
	int n;
	int found = 0;
	for (n = 1; n <= *node_size; n++) {
		if ((nodes[n].inst_start <= inst) &&
			(nodes[n].inst_end >= inst)) {
			found = n;
			break;
		}
	}
	return found;
}

int node_mid_start_add(struct control_flow_node_s *node, struct node_mid_start_s *node_mid_start, int path, int step)
{
	int n;
	int limit = node->next_size;
	int index = 1;

	for (n = 0; n < 1000; n++) {
		if (node_mid_start[n].node == 0) {
			node_mid_start[n].node = node->next_node[index];
			node_mid_start[n].path_prev = path;
			node_mid_start[n].path_prev_index = step;
			index++;
			if (index >= limit) {
				break;
			}
		}
	}
	return 0;
}

int path_loop_check(struct path_s *paths, int path, int step, int node, int limit)
{
	int tmp;
	int path1 = path;

	int n = 0;

	while (n < limit) {
		n++;
		step--;
		if (step < 0) {
			//printf("step < 0: 0x%x, 0x%x\n", paths[path].path_prev, paths[path].path_prev_index);
			if (paths[path].path_prev != path) {
				tmp = paths[path].path_prev;
				step = paths[path].path_prev_index;
				path = tmp;
			} else {
			// printf("No loop\n");
				return 0;
			}
		}
		//printf("loop_check: path=0x%x, step=0x%x, path_step=0x%x, node=0x%x\n",
		//	path, step, paths[path].path[step], node);
		if (paths[path].path[step] ==  node) {
			// printf("Loop found\n");
			paths[path1].type = 1;
			return 1;
		}
	};
	if (n >= limit) {
		/* The maximum lenght of a path is the number of nodes */
		printf("loop check limit reached\n");
		return 2;
	}
	return 0;
}


int merge_path_into_loop(struct path_s *paths, struct loop_s *loop, int path)
{
	int step;
	int tmp;
	int found;
	int n;
	int *list = loop->list;

	printf("trying to merge path %d into loop\n", path);

	loop->head = paths[path].loop_head;
	step = paths[path].path_size - 1; /* convert size to index */
	if (paths[path].path[step] != loop->head) {
		printf("merge_path failed path 0x%x != head 0x%x\n", paths[path].path[step], loop->head);
		exit(1);
	}
	while (1) {
		step--;
		if (step < 0) {
			/* If path_prev == path, we have reached the beginning of the path list */
			if (paths[path].path_prev != path) {
				tmp = paths[path].path_prev;
				step = paths[path].path_prev_index;
				path = tmp;
			} else {
				printf("No loop\n");
				return 0;
			}
		}
		found = 0;
		for (n = 0; n  < loop->size; n++) {
			if (list[n] == paths[path].path[step]) {
				found = 1;
				break;	
			}
		}
		if (!found) {
			printf("Merge: adding 0x%x\n",  paths[path].path[step]);
			tmp = paths[path].path[step];
			list[loop->size] = tmp;
			loop->size++;
		}

		if (paths[path].path[step] == loop->head) {
			printf("Start of merge Loop found\n");
			break;
		}
	}
	printf("merged head = 0x%x, size = 0x%x\n", loop->head, loop->size);
	return 0;
}


/* This is used to merge all the nodes of a loop with a particular loop_head */
/* It is then used to detect which node prev/next links exit the loop */
/* Work then then be done to scan the loops, and if only one loop_head exists in the loop, it is a single loop. */
/* If more than one loop_head exists in the loop, then it is a nested loop, or a disjointed loop */

int build_control_flow_loops(struct self_s *self, struct path_s *paths, int *paths_size, struct loop_s *loops, int *loop_size)
{
	int n;
	int m;
	int found;
	struct loop_s *loop;


	for (n = 0; n < *paths_size; n++) {
		if (paths[n].loop_head != 0) {
			found = -1;
			for(m = 0; m < *loop_size; m++) {
				if (loops[m].head == paths[n].loop_head) {
					found = m;
					printf("flow_loops found = %d\n", found);
					break;
				}
			}
			if (found == -1) {
				for(m = 0; m < *loop_size; m++) {
					if (loops[m].head == 0) {
						found = m;
						printf("flow_loops2 found = %d\n", found);
						break;
					}
				}
			}
			if (found == -1) {
				printf("build_control_flow_loops problem\n");
				exit(1);
			}
			if (found >= *loop_size) {
				printf("build_control_flow_loops problem2\n");
				exit(1);
			}
			loop = &loops[found];
			merge_path_into_loop(paths, loop, n);
		}
	}
	return 0;
}

int print_control_flow_loops(struct self_s *self, struct loop_s *loops, int *loops_size)
{
	int n, m;

	printf("Printing loops size = %d\n", *loops_size);
	for (m = 0; m < *loops_size; m++) {
		if (loops[m].size > 0) {
			printf("Loop %d: loop_head=%d\n", m, loops[m].head);
			for (n = 0; n < loops[m].size; n++) {
				printf("Loop %d=0x%x\n", m, loops[m].list[n]);
			}
		}
	}
	return 0;
}

int add_path_to_node(struct control_flow_node_s *node, int path)
{
	int size;
	int n;

	size = node->path_size;
	/* Don't add path twice */
	if (size > 0) {
		for (n = 0; n < size; n++) {
			if (node->path[n] == path) {
				return 1;
			}
		}
	}

	size++;
	node->path = realloc(node->path, size * sizeof(int));
	node->path[size - 1] = path;
	node->path_size = size;

	return 0;
}

int add_looped_path_to_node(struct control_flow_node_s *node, int path)
{
	int size;
	int n;

	size = node->looped_path_size;
	/* Don't add path twice */
	if (size > 0) {
		for (n = 0; n < size; n++) {
			if (node->looped_path[n] == path) {
				return 1;
			}
		}
	}

	size++;
	node->looped_path = realloc(node->looped_path, size * sizeof(int));
	node->looped_path[size - 1] = path;
	node->looped_path_size = size;

	return 0;
}


/* Is "a" a subset of "b" */
/* Are all the elements of "a" contained in "b" ? */
/* 0 = No */
/* 1 = Exact */
/* 2 = subset */
int is_subset(int size_a, int *a, int size_b, int *b)
{
	int tmp;
	int result = 0;
	int n,m;
	int found = 0;

	/* Optimisation 1 */
	if (size_b < size_a) {
		goto is_subset_exit;
	}
	/* Optimisation 2 */
	if (size_b == size_a) {
		tmp = memcmp(a , b, size_a * sizeof(int));
		if (!tmp) {
			result = 1;
			goto is_subset_exit;
		}
	}
	/* Handle the case of size_b > size_a */
	for (n = 0; n < size_a; n++) {
		found = 0;
		for (m = n; m < size_b; m++) {
			if (a[n] == b[m]) {
				found = 1;
				break;
			}
		}
		if (!found) {
			goto is_subset_exit;
		}
	}
	result = 2;

is_subset_exit:
	return result;
}

int build_node_dominance(struct self_s *self, struct control_flow_node_s *nodes, int *nodes_size)
{
	int n;
	int node_b = 1;
	int tmp;

	for(n = 1; n <= *nodes_size; n++) {
		node_b = n;
		while (node_b != 0) {
			/* FIXME: avoid following loop edge ones */
			tmp = nodes[node_b].prev_node[0];
			node_b = tmp;
			if (0 == node_b) {
				break;
			}
			tmp = is_subset(nodes[n].path_size, nodes[n].path, nodes[node_b].path_size, nodes[node_b].path);
			if (tmp) {
				nodes[n].dominator = node_b;
				if (1 == tmp) {
					/* 1 == exact match */
					if (nodes[node_b].next_size > 1) {
						/* dominator is a branch and we are the if_tail */
						nodes[node_b].if_tail = n;
					}
				break;
				}
			}
		}
	}

	return 0;
}

int build_node_paths(struct self_s *self, struct control_flow_node_s *nodes, int *node_size, struct path_s *paths, int *paths_size)

{
	int l,m,n;
	int path;
	int offset;

	printf("paths_size = %d\n", *paths_size);
	for (l = 0; l < *paths_size; l++) {
		path = l;
		offset = paths[l].path_size - 1;
		if (paths[l].path_size > 0) {
			while (1) {
				printf("Path=0x%x, offset=%d, Node=0x%x\n", l, offset, paths[path].path[offset]);
				if (paths[l].type == 1) {
					add_looped_path_to_node(&(nodes[paths[path].path[offset]]), l);
				} else {
					add_path_to_node(&(nodes[paths[path].path[offset]]), l);
				}
				offset--;
				if (offset < 0) {
					offset = paths[path].path_prev_index;
					if (path == paths[path].path_prev) {
						break;
					}
					path = paths[path].path_prev;
				}
			};
		}

	}
	return 0;
}

int build_control_flow_paths(struct self_s *self, struct control_flow_node_s *nodes, int *nodes_size, struct path_s *paths, int *paths_size, int *paths_used, int node_start)
{
	struct node_mid_start_s *node_mid_start;
	int found = 0;
	int path = 0;
	int step = 0;
	int n;
	int l;
	int m;
	int node = 1;
	int tmp;
	int loop = 0;

	node_mid_start = calloc(1000, sizeof(struct node_mid_start_s));

	node_mid_start[0].node = node_start;
	node_mid_start[0].path_prev = 0;
	node_mid_start[0].path_prev_index = 0;

	do {
		found = 0;
		for (n = 0; n < 1000; n++) {
			if (node_mid_start[n].node != 0) {
				found = 1;
				break;
			}
		}
		if (found == 1) {
			step = 0;
			node = node_mid_start[n].node;
			paths[path].used = 1;
			paths[path].path[step] = node;
			paths[path].path_prev = node_mid_start[n].path_prev;
			paths[path].path_prev_index = node_mid_start[n].path_prev_index;
			printf("JCD1: path 0x%x:0x%x, 0x%x\n", path, step, node_mid_start[n].node);
			node_mid_start[n].node = 0;
			step++;
			loop = 0;
			do {
				loop = path_loop_check(paths, path, step - 1, node, *nodes_size);

				if (loop) {
					printf("JCD0: path = 0x%x, step = 0x%x, node = 0x%x, loop = %d\n", path, step, node, loop);
					paths[path].loop_head = node;
					nodes[node].type = 1;
					nodes[node].loop_head = 1;
					/* Loops with more than one block */
					if (step >= 2) {
						int node1 = paths[path].path[step - 2];
						int node2 = paths[path].path[step - 1];
						printf("JCD4:loop: 0x%x, 0x%x\n", paths[path].path[step - 2], paths[path].path[step - 1]);
						for (n = 0; n < nodes[node2].prev_size; n++) {
							if (nodes[node2].prev_node[n] == node1) {
								nodes[node2].prev_type[n] = 2;
							}
						}
						for (n = 0; n < nodes[node1].next_size; n++) {
							if (nodes[node1].next_node[n] == node2) {
								nodes[node1].next_type[n] = 2;
							}
						}
					} else {
					//if (path && node == paths[paths[path].path_prev].path[paths[path].path_prev_index]) {
						printf("JCD1: do while loop to self on node = 0x%x, step = 0x%x, path=0x%x\n", node, step, path);
						paths[path].loop_head = node;
						nodes[node].type = 1;
						nodes[node].loop_head = 1;
						for (n = 0; n < nodes[node].prev_size; n++) {
							if (nodes[node].prev_node[n] == node) {
								nodes[node].prev_type[n] = 2;
							}
						}
						for (n = 0; n < nodes[node].next_size; n++) {
							if (nodes[node].next_node[n] == node) {
								nodes[node].next_type[n] = 2;
							}
						}
						break;
					}
				} else if (nodes[node].next_size == 1) {
					printf("JCD2: path 0x%x:0x%x, 0x%x -> 0x%x\n", path, step, node, nodes[node].next_node[0]);
					node = nodes[node].next_node[0];
					paths[path].path[step] = node;
					step++;
				} else if (nodes[node].next_size > 1) {
					tmp = node_mid_start_add(&nodes[node], node_mid_start, path, step - 1);
					printf("JCD3: path 0x%x:0x%x, 0x%x -> 0x%x\n", path, step, node, nodes[node].next_node[0]);
					node = nodes[node].next_node[0];
					paths[path].path[step] = node;
					step++;
				}
			} while ((nodes[node].next_size > 0) && (loop == 0));
			paths[path].path_size = step;
			path++;
			printf("end path = 0x%x\n", path);
			if (path >= *paths_size) {
				printf("TOO MANY PATHS, %d\n", path);
				return 1;
			}
		}
	} while (found == 1);
	free (node_mid_start);
	*paths_used = path;
	return 0;
}

int print_control_flow_paths(struct self_s *self, struct path_s *paths, int *paths_size)
{
	int n, m;
	printf("print control flow paths size=0x%x\n", *paths_size);
	for (m = 0; m < *paths_size; m++) {
		if (paths[m].used) {
			printf("Path 0x%x: type=%d, loop_head=0x%x, prev 0x%x:0x%x\n", m, paths[m].type, paths[m].loop_head, paths[m].path_prev, paths[m].path_prev_index);
			for (n = 0; n < paths[m].path_size; n++) {
				printf("Path 0x%x=0x%x\n", m, paths[m].path[n]);
			}
//		} else {
			//printf("Un-used Path 0x%x: type=%d, loop_head=0x%x, prev 0x%x:0x%x\n", m, paths[m].type, paths[m].loop_head, paths[m].path_prev, paths[m].path_prev_index);
		}

	}
	return 0;
}

int build_control_flow_nodes(struct self_s *self, struct control_flow_node_s *nodes, int *node_size)
{
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;
	int node = 1;
	int inst_start = 1;
	int inst_end;
	int n;
	int m;
	int tmp;

	printf("build_control_flow_nodes:\n");	
	for (n = 1; n <= inst_log; n++) {
		inst_log1 =  &inst_log_entry[n];
		/* Test for end of node */
		if ((inst_log1->next_size > 1) ||
			(inst_log1->next_size == 0) ||
			((inst_log1->next_size == 1) && (inst_log1->next[0] != (n + 1)))) {
			inst_end = n;
			/* Handle special case of duplicate prev_inst */
			/* FIXME: Stop duplicate prev_inst being created in the first place */
			if (inst_end >= inst_start) {
				nodes[node].inst_start = inst_start;
				nodes[node].inst_end = inst_end;
				node++;
				inst_start = n + 1;
			}
		}
		if (inst_log1->prev_size > 1) {
			inst_end = n - 1;
			/* Handle special case of duplicate prev_inst */
			/* FIXME: Stop duplicate prev_inst being created in the first place */
			if (inst_end >= inst_start) {
				nodes[node].inst_start = inst_start;
				nodes[node].inst_end = inst_end;
				node++;
				inst_start = n;
			}
		}
	}
	*node_size = node - 1;

	for (n = 1; n <= *node_size; n++) {
		inst_log1 =  &inst_log_entry[nodes[n].inst_start];
		if (inst_log1->prev_size > 0) {
			nodes[n].prev_node = calloc(inst_log1->prev_size, sizeof(int));
			nodes[n].prev_type = calloc(inst_log1->prev_size, sizeof(int));
			nodes[n].prev_size = inst_log1->prev_size;

			for (m = 0; m < inst_log1->prev_size; m++) {
				tmp = find_node_from_inst(self, nodes, node_size, inst_log1->prev[m]);
				nodes[n].prev_node[m] = tmp;
			}
		}
		inst_log1 =  &inst_log_entry[nodes[n].inst_end];
		if (inst_log1->next_size > 0) {
			nodes[n].next_node = calloc(inst_log1->next_size, sizeof(int));
			nodes[n].next_type = calloc(inst_log1->next_size, sizeof(int));
			nodes[n].next_size = inst_log1->next_size;

			for (m = 0; m < inst_log1->next_size; m++) {
				tmp = find_node_from_inst(self, nodes, node_size, inst_log1->next[m]);
				nodes[n].next_node[m] = tmp;
			}
		}
	}
	return 0;
}



int print_control_flow_nodes(struct self_s *self, struct control_flow_node_s *nodes, int *node_size)
{
	int n;
	int m;

	printf("print_control_flow_nodes:\n");	
	for (n = 1; n <= *node_size; n++) {
		printf("Node:0x%x, type=%d, dominator=0x%x, if_tail=0x%x, loop_head=%d, inst_start=0x%x, inst_end=0x%x\n",
			n,
			nodes[n].type,
			nodes[n].dominator,
			nodes[n].if_tail,
			nodes[n].loop_head,
			nodes[n].inst_start,
			nodes[n].inst_end);
		for (m = 0; m < nodes[n].prev_size; m++) {
			printf("nodes[0x%x].prev_node[%d] = 0x%x, prev_type=%d\n", n, m, nodes[n].prev_node[m], nodes[n].prev_type[m]);
		}
		for (m = 0; m < nodes[n].next_size; m++) {
			printf("nodes[0x%x].next_node[%d] = 0x%x, next_type=%d\n", n, m, nodes[n].next_node[m], nodes[n].next_type[m]);
		}
		printf("nodes[0x%x].path_size = 0x%x\n", n, nodes[n].path_size);
		printf("nodes[0x%x].looped_size = 0x%x\n", n, nodes[n].looped_path_size);
//		for (m = 0; m < nodes[n].path_size; m++) {
//			printf("nodes[0x%x].path[%d] = 0x%x\n", n, m, nodes[n].path[m]);
//		}
//		for (m = 0; m < nodes[n].looped_path_size; m++) {
//			printf("nodes[0x%x].looped_path[%d] = 0x%x\n", n, m, nodes[n].looped_path[m]);
//		}

	}
	return 0;
}

/* For each node in each loop list, check each node->next[] to see whether it is exiting the loop or not. */
int analyse_control_flow_loop_exits(struct self_s *self, struct control_flow_node_s *nodes, int *node_size, struct loop_s *loops, int *loop_size)
{
	int l, m, n, n2;
	struct control_flow_node_s *node;
	int head;
	int next;
	int type;
	int found;

	for (l = 0; l < *loop_size; l++) {
		for (m = 0; m < loops[l].size; m++) {
			node = &nodes[loops[l].list[m]];
			head = loops[l].head;
			for (n = 0; n < node->next_size; n++) {
				next = node->next_node[n];
				type = node->next_type[n];
				if (type == 0) {
					/* Only modify when the type is normal == 0 */
					found = 0;
					for (n2 = 0; n2 < loops[l].size; n2++) {
						if (next == loops[l].list[n2]) {
							found = 1;
							break;
						}
					}
					if (!found) {
						node->next_type[n] = 3;  /* exit type */
					}
				}
			}
		}
	}
	return 0;
}


int get_value_from_index(struct operand_s *operand, uint64_t *index)
{
	if (operand->indirect) {
		printf(" %s%s[%s0x%"PRIx64"],",
			size_table[operand->value_size],
			indirect_table[operand->indirect],
			store_table[operand->store],
			operand->index);
	} else {
		printf(" %s%s0x%"PRIx64",",
		size_table[operand->value_size],
		store_table[operand->store],
		operand->index);
	}
	return 1;
}

int ram_init(struct memory_s *memory_data)
{
	return 0;
}

int reg_init(struct memory_s *memory_reg)
{
	/* esp */
	memory_reg[0].start_address = REG_SP;
	/* 4 bytes */
	memory_reg[0].length = 8;
	/* 1 - Known */
	memory_reg[0].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[0].init_value = 0x10000;
	/* No offset yet */
	memory_reg[0].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[0].value_type = 6;
	memory_reg[0].value_unsigned = 0;
	memory_reg[0].value_signed = 0;
	memory_reg[0].value_instruction = 0;
	memory_reg[0].value_pointer = 1;
	memory_reg[0].value_normal = 0;
	/* Index into the various structure tables */
	memory_reg[0].value_struct = 0;
	/* last_accessed_from_instruction_at_memory_location */
	memory_reg[0].ref_memory = 0;
	memory_reg[0].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[0].value_scope = 2;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[0].value_id = 1;
	/* valid: 0 - Entry Not used yet, 1 - Entry Used */
	memory_reg[0].valid = 1;

	/* ebp */
	memory_reg[1].start_address = REG_BP;
	/* 4 bytes */
	memory_reg[1].length = 8;
	/* 1 - Known */
	memory_reg[1].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[1].init_value = 0x20000;
	/* No offset yet */
	memory_reg[1].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[1].value_type = 6;
	memory_reg[1].value_unsigned = 0;
	memory_reg[1].value_signed = 0;
	memory_reg[1].value_instruction = 0;
	memory_reg[1].value_pointer = 1;
	memory_reg[1].value_normal = 0;
	/* Index into the various structure tables */
	memory_reg[1].value_struct = 0;
	memory_reg[1].ref_memory = 0;
	memory_reg[1].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[1].value_scope = 2;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[1].value_id = 2;
	/* valid: 0 - entry Not used yet, 1 - entry Used */
	memory_reg[1].valid = 1;

	/* eip */
	memory_reg[2].start_address = REG_IP;
	/* 4 bytes */
	memory_reg[2].length = 8;
	/* 1 - Known */
	memory_reg[2].init_value_type = 1;
	/* Initial value when first accessed */
	memory_reg[2].init_value = EIP_START;
	/* No offset yet */
	memory_reg[2].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_reg[2].value_type = 5;
	memory_reg[2].value_type = 6;
	memory_reg[2].value_unsigned = 0;
	memory_reg[2].value_signed = 0;
	memory_reg[2].value_instruction = 0;
	memory_reg[2].value_pointer = 1;
	memory_reg[2].value_normal = 0;
	/* Index into the various structure tables */
	memory_reg[2].value_struct = 0;
	memory_reg[2].ref_memory = 0;
	memory_reg[2].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_reg[2].value_scope = 3;
	/* Each time a new value is assigned, this value_id increases */
	memory_reg[2].value_id = 0;
	/* valid: 0 - entry Not used yet, 1 - entry Used */
	memory_reg[2].valid = 1;
	return 0;
}

int stack_init(struct memory_s *memory_stack)
{
	int n = 0;
	/* eip on the stack */
	memory_stack[n].start_address = 0x10000;
	/* 4 bytes */
	memory_stack[n].length = 8;
	/* 1 - Known */
	memory_stack[n].init_value_type = 1;
	/* Initial value when first accessed */
	memory_stack[n].init_value = 0x0;
	/* No offset yet */
	memory_stack[n].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_stack[n].value_type = 5;
	memory_stack[n].value_unsigned = 0;
	memory_stack[n].value_signed = 0;
	memory_stack[n].value_instruction = 0;
	memory_stack[n].value_pointer = 1;
	memory_stack[n].value_normal = 0;
	/* Index into the various structure tables */
	memory_stack[n].value_struct = 0;
	memory_stack[n].ref_memory = 0;
	memory_stack[n].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_stack[n].value_scope = 2;
	/* Each time a new value is assigned, this value_id increases */
	memory_stack[n].value_id = 3;
	/* valid: 0 - Not used yet, 1 - Used */
	memory_stack[n].valid = 1;
	n++;

#if 0
	/* Param1 */
	memory_stack[n].start_address = 0x10004;
	/* 4 bytes */
	memory_stack[n].length = 4;
	/* 1 - Known */
	memory_stack[n].init_value_type = 1;
	/* Initial value when first accessed */
	memory_stack[n].init_value = 0x321;
	/* No offset yet */
	memory_stack[n].offset_value = 0;
	/* 0 - unknown,
	 * 1 - unsigned,
	 * 2 - signed,
	 * 3 - pointer,
	 * 4 - Instruction,
	 * 5 - Instruction pointer(EIP),
	 * 6 - Stack pointer.
	 */
	memory_stack[n].value_type = 2;
	memory_stack[n].ref_memory = 0;
	memory_stack[n].ref_log = 0;
	/* value_scope: 0 - unknown, 1 - Param, 2 - Local, 3 - Mem */
	memory_stack[n].value_scope = 0;
	/* Each time a new value is assigned, this value_id increases */
	memory_stack[n].value_id = 0;
	/* valid: 0 - Not used yet, 1 - Used */
	memory_stack[n].valid = 1;
	n++;
#endif
	for (;n < MEMORY_STACK_SIZE; n++) {
		memory_stack[n].valid = 0;
	}
	return 0;
}

int print_mem(struct memory_s *memory, int location) {
	printf("start_address:0x%"PRIx64"\n",
		memory[location].start_address);
	printf("length:0x%x\n",
		memory[location].length);
	printf("init_value_type:0x%x\n",
		memory[location].init_value_type);
	printf("init:0x%"PRIx64"\n",
		memory[location].init_value);
	printf("offset:0x%"PRIx64"\n",
		memory[location].offset_value);
	printf("indirect_init:0x%"PRIx64"\n",
		memory[location].indirect_init_value);
	printf("indirect_offset:0x%"PRIx64"\n",
		memory[location].indirect_offset_value);
	printf("value_type:0x%x\n",
		memory[location].value_type);
	printf("ref_memory:0x%"PRIx32"\n",
		memory[location].ref_memory);
	printf("ref_log:0x%"PRIx32"\n",
		memory[location].ref_log);
	printf("value_scope:0x%x\n",
		memory[location].value_scope);
	printf("value_id:0x%"PRIx64"\n",
		memory[location].value_id);
	printf("valid:0x%"PRIx64"\n",
		memory[location].valid);
	return 0;
}

/************************************************************
 * This function uses information from instruction log entries
 * and creates labels.
 ************************************************************/
int log_to_label(int store, int indirect, uint64_t index, uint64_t relocated, uint64_t value_scope, uint64_t value_id, uint64_t indirect_offset_value, uint64_t indirect_value_id, struct label_s *label) {
	int tmp;

	/* FIXME: May handle by using first switch as switch (indirect) */
	printf("value in log_to_label: store=0x%x, indirect=0x%x, index=0x%"PRIx64", relocated = 0x%"PRIx64", scope = 0x%"PRIx64", id = 0x%"PRIx64", ind_off_value = 0x%"PRIx64", ind_val_id = 0x%"PRIx64"\n",
				store,
				indirect,
				index,
				relocated,
				value_scope,
				value_id,
				indirect_offset_value,
				indirect_value_id);


	switch (store) {
	case STORE_DIRECT:
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		/* FIXME: relocation table not present in 16bit x86 mode, so another method will need to be found */
		if (indirect == IND_MEM) {
			label->scope = 3;
			label->type = 1;
			label->lab_pointer = 1;
			label->value = index;
		} else if (relocated) {
			label->scope = 3;
			label->type = 2;
			label->lab_pointer = 0;
			label->value = index;
		} else {
			label->scope = 3;
			label->type = 3;
			label->lab_pointer = 0;
			label->value = index;
		}
		break;
	case STORE_REG:
		switch (value_scope) {
		case 1:
			/* params */
			if (IND_STACK == indirect) {
				label->scope = 2;
				label->type = 2;
				label->lab_pointer = 0;
				label->value = indirect_offset_value;
				printf("PARAM_STACK^\n"); 
			} else if (0 == indirect) {
				label->scope = 2;
				label->type = 1;
				label->lab_pointer = 0;
				label->value = index;
				printf("PARAM_REG^\n"); 
			} else {
				printf("JCD: UNKNOWN PARAMS\n");
			}
			break;
		case 2:
			/* locals */
			if (IND_STACK == indirect) {
				label->scope = 1;
				label->type = 2;
				label->lab_pointer = 0;
				label->value = value_id;
			} else if (0 == indirect) {
				label->scope = 1;
				label->type = 1;
				label->lab_pointer = 0;
				label->value = value_id;
			} else {
				printf("JCD: UNKNOWN LOCAL\n");
			}
			break;
		case 3: /* Data */
			/* FIXME: introduce indirect_value_id and indirect_value_scope */
			/* in order to resolve somewhere */
			/* It will always be a register, and therefore can re-use the */
			/* value_id to identify it. */
			/* It will always be a local and not a param */
			/* FIXME: This should be handled scope = 1, type = 1 above. */
			/* was scope = 4*/
			/* FIXME: get the label->value right */
			label->scope = 1;
			label->type = 1;
			label->lab_pointer = 1;
			label->value = indirect_value_id;
			break;
		default:
			label->scope = 0;
			label->type = value_scope;
			label->lab_pointer = 0;
			label->value = 0;
			printf("unknown value scope: %04"PRIx64";\n", (value_scope));
			return 1;
			break;
		}
		break;
	default:
		printf("Unhandled store1\n");
		return 1;
		break;
	}
	return 0;
}

int output_label_redirect(int offset, struct label_s *labels, struct label_redirect_s *label_redirect, FILE *fd) {
	int tmp;
	struct label_s *label;

	tmp = label_redirect[offset].redirect;
	label = &labels[tmp];
	tmp = output_label(label, fd);
	return 0;
}

int output_label(struct label_s *label, FILE *fd) {
	int tmp;

	switch (label->scope) {
	case 3:
		printf("%"PRIx64";\n", label->value);
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		switch (label->type) {
		case 1:
			tmp = fprintf(fd, "data%04"PRIx64,
				label->value);
			break;
		case 2:
			tmp = fprintf(fd, "&data%04"PRIx64,
				label->value);
			break;
		case 3:
			tmp = fprintf(fd, "0x%"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error\n");
			return 1;
			break;
		}
		break;
	case 2:
		switch (label->type) {
		case 2:
			printf("param_stack%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "param_stack%04"PRIx64,
				label->value);
			break;
		case 1:
			printf("param_reg%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "param_reg%04"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error\n");
			return 1;
			break;
		}
		break;
	case 1:
		switch (label->type) {
		case 2:
			printf("local_stack%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "local_stack%04"PRIx64,
				label->value);
			break;
		case 1:
			printf("local_reg%04"PRIx64,
				label->value);
			tmp = fprintf(fd, "local_reg%04"PRIx64,
				label->value);
			break;
		default:
			printf("output_label error type=%"PRIx64"\n", label->type);
			return 1;
			break;
		}
		break;
	case 4:
		/* FIXME: introduce indirect_value_id and indirect_value_scope */
		/* in order to resolve somewhere */
		/* It will always be a register, and therefore can re-use the */
		/* value_id to identify it. */
		/* It will always be a local and not a param */
		/* FIXME: local_reg should be handled in case 1.1 above and
		 *        not be a separate label
		 */
		printf("xxxlocal_reg%04"PRIx64";\n", label->value);
		tmp = fprintf(fd, "xxxlocal_reg%04"PRIx64,
			label->value);
		break;
	default:
		printf("unknown label scope: %04"PRIx64";\n", label->scope);
		tmp = fprintf(fd, "unknown%04"PRIx64,
			label->scope);
		break;
	}
	return 0;
}

int output_variable(int store, int indirect, uint64_t index, uint64_t relocated, uint64_t value_scope, uint64_t value_id, uint64_t indirect_offset_value, uint64_t indirect_value_id, FILE *fd) {
	int tmp;
	/* FIXME: May handle by using first switch as switch (indirect) */
	switch (store) {
	case STORE_DIRECT:
		printf("%"PRIx64";\n", index);
		/* FIXME: Handle the case of an immediate value being &data */
		/* but it is very difficult to know if the value is a pointer (&data) */
		/* or an offset (data[x]) */
		/* need to use the relocation table to find out */
		/* no relocation table entry == offset */
		/* relocation table entry == pointer */
		/* this info should be gathered at disassembly point */
		if (indirect == IND_MEM) {
			tmp = fprintf(fd, "data%04"PRIx64,
				index);
		} else if (relocated) {
			tmp = fprintf(fd, "&data%04"PRIx64,
				index);
		} else {
			tmp = fprintf(fd, "0x%"PRIx64,
				index);
		}
		break;
	case STORE_REG:
		switch (value_scope) {
		case 1:
			/* FIXME: Should this be param or instead param_reg, param_stack */
			if (IND_STACK == indirect) {
				printf("param_stack%04"PRIx64",%04"PRIx64",%04d",
					index, indirect_offset_value, indirect);
				tmp = fprintf(fd, "param_stack%04"PRIx64",%04"PRIx64",%04d",
					index, indirect_offset_value, indirect);
			} else if (0 == indirect) {
				printf("param_reg%04"PRIx64,
					index);
				tmp = fprintf(fd, "param_reg%04"PRIx64,
					index);
			}
			break;
		case 2:
			/* FIXME: Should this be local or instead local_reg, local_stack */
			if (IND_STACK == indirect) {
				printf("local_stack%04"PRIx64,
					value_id);
				tmp = fprintf(fd, "local_stack%04"PRIx64,
					value_id);
			} else if (0 == indirect) {
				printf("local_reg%04"PRIx64,
					value_id);
				tmp = fprintf(fd, "local_reg%04"PRIx64,
					value_id);
			}
			break;
		case 3: /* Data */
			/* FIXME: introduce indirect_value_id and indirect_value_scope */
			/* in order to resolve somewhere */
			/* It will always be a register, and therefore can re-use the */
			/* value_id to identify it. */
			/* It will always be a local and not a param */
			printf("xxxlocal_mem%04"PRIx64";\n", (indirect_value_id));
			tmp = fprintf(fd, "xxxlocal_mem%04"PRIx64,
				indirect_value_id);
			break;
		default:
			printf("unknown value scope: %04"PRIx64";\n", (value_scope));
			tmp = fprintf(fd, "unknown%04"PRIx64,
				value_scope);
			break;
		}
		break;
	default:
		printf("Unhandled store1\n");
		break;
	}
	return 0;
}

int if_expression( int condition, struct inst_log_entry_s *inst_log1_flagged,
	struct label_redirect_s *label_redirect, struct label_s *labels, FILE *fd)
{
	int opcode;
	int err = 0;
	int tmp;
	int store;
	int indirect;
	uint64_t index;
	uint64_t relocated;
	uint64_t value_scope;
	uint64_t value_id;
	uint64_t indirect_offset_value;
	uint64_t indirect_value_id;
	struct label_s *label;
	const char *condition_string;

	opcode = inst_log1_flagged->instruction.opcode;
	printf("\t if opcode=0x%x, ",inst_log1_flagged->instruction.opcode);

	switch (opcode) {
	case CMP:
		switch (condition) {
		case LESS_EQUAL:
		case BELOW_EQUAL:   /* Unsigned */
			condition_string = " <= ";
			break;
		case GREATER_EQUAL: /* Signed */
		case NOT_BELOW:   /* Unsigned */
			condition_string = " >= ";
			break;
		case GREATER:
		case ABOVE:
			condition_string = " > ";
			break;
		case BELOW:
		case LESS:
			condition_string = " < ";
			break;
		case NOT_EQUAL:
			condition_string = " != ";
			break;
		case EQUAL:
			condition_string = " == ";
			break;
		default:
			printf("if_expression: non-yet-handled: 0x%x\n", condition);
			err = 1;
			break;
		}
		if (err) break;
		tmp = fprintf(fd, "(");
		if (IND_MEM == inst_log1_flagged->instruction.dstA.indirect) {
			tmp = fprintf(fd, "*");
			value_id = inst_log1_flagged->value2.indirect_value_id;
		} else {
			value_id = inst_log1_flagged->value2.value_id;
		}
		if (STORE_DIRECT == inst_log1_flagged->instruction.dstA.store) {
			tmp = fprintf(fd, "0x%"PRIx64, inst_log1_flagged->instruction.dstA.index);
		} else {
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
		}
		tmp = fprintf(fd, "%s", condition_string);
		if (IND_MEM == inst_log1_flagged->instruction.srcA.indirect) {
			tmp = fprintf(fd, "*");
			value_id = inst_log1_flagged->value1.indirect_value_id;
		} else {
			value_id = inst_log1_flagged->value1.value_id;
		}
		tmp = label_redirect[value_id].redirect;
		label = &labels[tmp];
		//tmp = fprintf(fd, "0x%x:", tmp);
		tmp = output_label(label, fd);
		tmp = fprintf(fd, ") ");
		break;
	case SUB:
	case ADD:
		switch (condition) {
		case EQUAL:
			condition_string = " == 0";
			break;
		case NOT_EQUAL:
			condition_string = " != 0";
			break;
		default:
			printf("if_expression: non-yet-handled: 0x%x\n", condition);
			err = 1;
			break;
		}

		if ((!err) && (IND_DIRECT == inst_log1_flagged->instruction.srcA.indirect) && 
			(IND_DIRECT == inst_log1_flagged->instruction.dstA.indirect) &&
			(0 == inst_log1_flagged->value3.offset_value)) {
			tmp = fprintf(fd, "((");
			if (1 == inst_log1_flagged->instruction.dstA.indirect) {
				tmp = fprintf(fd, "*");
				value_id = inst_log1_flagged->value2.indirect_value_id;
			} else {
				value_id = inst_log1_flagged->value2.value_id;
			}
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
			tmp = fprintf(fd, "%s) ", condition_string);
		}
		break;

	case TEST:
		switch (condition) {
		case EQUAL:
			condition_string = " == 0";
			break;
		case NOT_EQUAL:
			condition_string = " != 0";
			break;
		case LESS_EQUAL:
			condition_string = " <= 0";
			break;
		default:
			printf("if_expression: non-yet-handled: 0x%x\n", condition);
			err = 1;
			break;
		}

		if ((!err) && (IND_DIRECT == inst_log1_flagged->instruction.srcA.indirect) && 
			(IND_DIRECT == inst_log1_flagged->instruction.dstA.indirect) &&
			(0 == inst_log1_flagged->value3.offset_value)) {
			tmp = fprintf(fd, "((");
			if (1 == inst_log1_flagged->instruction.dstA.indirect) {
				tmp = fprintf(fd, "*");
				value_id = inst_log1_flagged->value2.indirect_value_id;
			} else {
				value_id = inst_log1_flagged->value2.value_id;
			}
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
			tmp = fprintf(fd, " AND ");
			if (1 == inst_log1_flagged->instruction.srcA.indirect) {
				tmp = fprintf(fd, "*");
				value_id = inst_log1_flagged->value1.indirect_value_id;
			} else {
				value_id = inst_log1_flagged->value1.value_id;
			}
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
			tmp = fprintf(fd, ")%s) ", condition_string);
		}
		break;

	case rAND:
		switch (condition) {
		case EQUAL:
			condition_string = " == 0";
			break;
		case NOT_EQUAL:
			condition_string = " != 0";
			break;
		default:
			printf("if_expression: non-yet-handled: 0x%x\n", condition);
			err = 1;
			break;
		}

		if ((!err) && (IND_DIRECT == inst_log1_flagged->instruction.srcA.indirect) && 
			(IND_DIRECT == inst_log1_flagged->instruction.dstA.indirect) &&
			(0 == inst_log1_flagged->value3.offset_value)) {
			tmp = fprintf(fd, "((");
			if (1 == inst_log1_flagged->instruction.dstA.indirect) {
				tmp = fprintf(fd, "*");
				value_id = inst_log1_flagged->value2.indirect_value_id;
			} else {
				value_id = inst_log1_flagged->value2.value_id;
			}
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
			tmp = fprintf(fd, " AND ");
			if (1 == inst_log1_flagged->instruction.srcA.indirect) {
				tmp = fprintf(fd, "*");
				value_id = inst_log1_flagged->value1.indirect_value_id;
			} else {
				value_id = inst_log1_flagged->value1.value_id;
			}
			tmp = label_redirect[value_id].redirect;
			label = &labels[tmp];
			//tmp = fprintf(fd, "0x%x:", tmp);
			tmp = output_label(label, fd);
			tmp = fprintf(fd, ")%s) ", condition_string);
		}
		break;

	default:
		printf("if_expression: Previous flags instruction not handled: opcode = 0x%x, cond = 0x%x\n", opcode, condition);
		err = 1;
		break;
	}
	return err;
}

/* If relocated_data returns 1, it means that there was a
 * relocation table entry for this data location.
 * This most likely means that this is a pointer.
 * FIXME: What to do if the relocation is to the code segment? Pointer to function?
 */
uint32_t relocated_data(struct rev_eng *handle, uint64_t offset, uint64_t size)
{
	int n;
	for (n = 0; n < handle->reloc_table_data_sz; n++) {
		if (handle->reloc_table_data[n].address == offset) {
			return 1;
		}
	}
	return 0;
}


uint32_t output_function_name(FILE *fd,
		struct external_entry_point_s *external_entry_point)
{
	int commas = 0;
	int tmp, n;

	printf("int %s()\n{\n", external_entry_point->name);
	printf("value = %"PRIx64"\n", external_entry_point->value);
	tmp = fprintf(fd, "int %s(", external_entry_point->name);
	return 0;
}

int output_function_body(struct self_s *self, struct process_state_s *process_state,
			 FILE *fd, int start, int end, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int tmp, l, m, n, n2;
	int tmp_state;
	int err;
	int found;
	uint64_t value_id;
	struct instruction_s *instruction;
	struct instruction_s *instruction_prev;
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log1_prev;
	struct inst_log_entry_s *inst_log1_flags;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;
	struct external_entry_point_s *external_entry_points = self->external_entry_points;
	struct memory_s *value;
	struct label_s *label;
	struct extension_call_s *call;

	if (!start || !end) {
		printf("output_function_body:Invalid start or end\n");
		return 1;
	}
	printf("output_function_body:start=0x%x, end=0x%x\n", start, end);

	for (n = start; n <= end; n++) {
		inst_log1 =  &inst_log_entry[n];
		if (!inst_log1) {
			printf("output_function_body:Invalid inst_log1[0x%x]\n", n);
			return 1;
		}
		inst_log1_prev =  &inst_log_entry[inst_log1->prev[0]];
		if (!inst_log1_prev) {
			printf("output_function_body:Invalid inst_log1_prev[0x%x]\n", n);
			return 1;
		}
		instruction =  &inst_log1->instruction;
		instruction_prev =  &inst_log1_prev->instruction;

		write_inst(self, fd, instruction, n, labels);
		
		tmp = fprintf(fd, "// ");
		if (inst_log1->prev_size > 0) {
			tmp = fprintf(fd, "prev_size=0x%x: ",
				inst_log1->prev_size);
			for (l = 0; l < inst_log1->prev_size; l++) {
				tmp = fprintf(fd, "prev=0x%x, ",
				inst_log1->prev[l]);
			}
		}
		if (inst_log1->next_size > 0) {
			tmp = fprintf(fd, "next_size=0x%x: ",
				inst_log1->next_size);
			for (l = 0; l < inst_log1->next_size; l++) {
				tmp = fprintf(fd, "next=0x%x, ",
				inst_log1->next[l]);
			}
		}
		tmp = fprintf(fd, "\n");
		/* Output labels when this is a join point */
		/* or when the previous instruction was some sort of jump */
		if ((inst_log1->prev_size) > 1) {
			printf("label%04"PRIx32":\n", n);
			tmp = fprintf(fd, "label%04"PRIx32":\n", n);
		} else {
			if ((inst_log1->prev[0] != (n - 1)) &&
				(inst_log1->prev[0] != 0)) {		
				printf("label%04"PRIx32":\n", n);
				tmp = fprintf(fd, "label%04"PRIx32":\n", n);
			}
		}
		printf("\n");
		/* Test to see if we have an instruction to output */
		printf("Inst 0x%04x: %d: value_type = %d, %d, %d\n", n,
			instruction->opcode,
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		/* FIXME: JCD: This fails for some call instructions */
		if ((0 == inst_log1->value3.value_type) ||
			(1 == inst_log1->value3.value_type) ||
			(2 == inst_log1->value3.value_type) ||
			(3 == inst_log1->value3.value_type) ||
			(4 == inst_log1->value3.value_type) ||
			(6 == inst_log1->value3.value_type) ||
			(5 == inst_log1->value3.value_type)) {
			switch (instruction->opcode) {
			case MOV:
			case SEX:
				if (inst_log1->value1.value_type == 6) {
					printf("ERROR1 %d\n", instruction->opcode);
					//break;
				}
				if (inst_log1->value1.value_type == 5) {
					printf("ERROR2\n");
					//break;
				}
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				/* FIXME: Check limits */
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " = ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");

				break;
			case NEG:
				if (inst_log1->value1.value_type == 6) {
					printf("ERROR1\n");
					//break;
				}
				if (inst_log1->value1.value_type == 5) {
					printf("ERROR2\n");
					//break;
				}
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				/* FIXME: Check limits */
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " = -");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");

				break;

			case ADD:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " += ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				//tmp = fprintf(fd, "0x%x:", tmp);
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case MUL:
			case IMUL:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " *= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SUB:
			case SBB:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " -= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case rAND:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " &= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case OR:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " |= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case XOR:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " ^= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case NOT:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " = !");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SHL: //TODO: UNSIGNED
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " <<= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SHR: //TODO: UNSIGNED
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " >>= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SAL: //TODO: SIGNED
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " <<= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case SAR: //TODO: SIGNED
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				if (1 == instruction->dstA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value3.value_id);
				tmp = fprintf(fd, " >>= ");
				printf("\nstore=%d\n", instruction->srcA.store);
				if (1 == instruction->srcA.indirect) {
					tmp = fprintf(fd, "*");
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = label_redirect[value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			case JMP:
				printf("JMP reached XXXX\n");
				if (print_inst(self, instruction, n, labels))
					return 1;
				tmp = fprintf(fd, "\t");

//				if (instruction->srcA.relocated) {
//					printf("JMP goto rel%08"PRIx64";\n", instruction->srcA.index);
//					tmp = fprintf(fd, "JMP goto rel%08"PRIx64";\n",
//						instruction->srcA.index);
//				} else {
					printf("JMP2 goto label%04"PRIx32";\n",
						inst_log1->next[0]);
					tmp = fprintf(fd, "JMP2 goto label%04"PRIx32";\n",
						inst_log1->next[0]);
//				}
				break;
			case CALL:
				/* FIXME: This does nothing at the moment. */
				if (print_inst(self, instruction, n, labels)) {
					tmp = fprintf(fd, "exiting1\n");
					return 1;
				}
				/* Search for EAX */
				printf("call index = 0x%"PRIx64"\n", instruction->srcA.index);
				tmp = instruction->srcA.index;
				if ((tmp >= 0) && (tmp < EXTERNAL_ENTRY_POINTS_MAX)) {
					printf("params size = 0x%x\n",
						external_entry_points[instruction->srcA.index].params_size);
				}
				printf("\t");
				tmp = fprintf(fd, "\t");
				tmp = label_redirect[inst_log1->value3.value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				printf(" = ");
				tmp = fprintf(fd, " = ");
				if (IND_DIRECT == instruction->srcA.indirect) {
					/* A direct call */
					/* FIXME: Get the output right */
					if (1 == instruction->srcA.relocated) {
						struct extension_call_s *call;
						call = inst_log1->extension;
						//tmp = fprintf(fd, "%s(%d:", 
						//	external_entry_points[instruction->srcA.index].name,
						//	external_entry_points[instruction->srcA.index].params_size);
						tmp = fprintf(fd, "%s(", 
							external_entry_points[instruction->srcA.index].name);
						tmp_state = 0;
						for (n2 = 0; n2 < call->params_size; n2++) {
							struct label_s *label;
							tmp = label_redirect[call->params[n2]].redirect;
							label = &labels[tmp];
							//printf("reg_params_order = 0x%x, label->value = 0x%"PRIx64"\n", reg_params_order[m], label->value);
							//if ((label->scope == 2) &&
							//	(label->type == 1)) {
							if (tmp_state > 0) {
								fprintf(fd, ", ");
							}
							//fprintf(fd, "int%"PRId64"_t ",
							//	label->size_bits);
							tmp = output_label(label, fd);
							tmp_state++;
						//	}
						}
#if 0
						for (n2 = 0; n2 < external_entry_points[l].params_size; n2++) {
							struct label_s *label;
							label = &labels[external_entry_points[l].params[n2]];
							if ((label->scope == 2) &&
								(label->type == 1)) {
								continue;
							}
							if (tmp_state > 0) {
								fprintf(fd, ", ");
							}
							fprintf(fd, "int%"PRId64"_t ",
							label->size_bits);
							tmp = output_label(label, fd);
							tmp_state++;
						}
#endif
						tmp = fprintf(fd, ");\n");
					} else {
						tmp = fprintf(fd, "CALL1()\n");
					}
#if 0
					/* FIXME: JCD test disabled */
					call = inst_log1->extension;
					if (call) {
						for (l = 0; l < call->params_size; l++) {
							if (l > 0) {
								fprintf(fd, ", ");
							}
							label = &labels[call->params[l]];
							tmp = output_label(label, fd);
						}
					}
#endif
					//tmp = fprintf(fd, ");\n");
					//printf("%s();\n",
					//	external_entry_points[instruction->srcA.index].name);
				} else {
					/* A indirect call via a function pointer or call table. */
					tmp = fprintf(fd, "(*");
					tmp = label_redirect[inst_log1->value1.indirect_value_id].redirect;
					label = &labels[tmp];
					tmp = output_label(label, fd);
					tmp = fprintf(fd, ") ()\n");
				}
//				tmp = fprintf(fd, "/* call(); */\n");
//				printf("/* call(); */\n");
				break;

			case CMP:
				/* Don't do anything for this instruction. */
				/* only does anything if combined with a branch instruction */
				if (print_inst(self, instruction, n, labels))
					return 1;
				tmp = fprintf(fd, "\t");
				tmp = fprintf(fd, "/* cmp; */\n");
				printf("/* cmp; */\n");
				break;

			case TEST:
				/* Don't do anything for this instruction. */
				/* only does anything if combined with a branch instruction */
				if (print_inst(self, instruction, n, labels))
					return 1;
				tmp = fprintf(fd, "\t");
				tmp = fprintf(fd, "/* test; */\n");
				printf("/* test; */\n");
				break;

			case IF:
				/* FIXME: Never gets here, why? */
				/* Don't do anything for this instruction. */
				/* only does anything if combined with a branch instruction */
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				printf("if ");
				tmp = fprintf(fd, "if ");
				found = 0;
				tmp = 30; /* Limit the scan backwards */
				l = inst_log1->prev[0];
				do {
					inst_log1_flags =  &inst_log_entry[l];
					printf("Previous opcode 0x%x\n", inst_log1_flags->instruction.opcode);
					printf("Previous flags 0x%x\n", inst_log1_flags->instruction.flags);
					if (1 == inst_log1_flags->instruction.flags) {
						found = 1;
					}
					printf("Previous flags instruction size 0x%x\n", inst_log1_flags->prev_size);
					if (inst_log1_flags->prev > 0) {
						l = inst_log1_flags->prev[0];
					} else {
						l = 0;
					}
					tmp--;
				} while ((0 == found) && (0 < tmp) && (0 != l));
				if (found == 0) {
					printf("Previous flags instruction not found. found=%d, tmp=%d, l=%d\n", found, tmp, l);
					return 1;
				} else {
					printf("Previous flags instruction found. found=%d, tmp=%d, l=%d\n", found, tmp, l);
				}
					
				err = if_expression( instruction->srcA.index, inst_log1_flags, label_redirect, labels, fd);
				printf("\t prev flags=%d, ",inst_log1_flags->instruction.flags);
				printf("\t prev opcode=0x%x, ",inst_log1_flags->instruction.opcode);
				printf("\t 0x%"PRIx64":%s", instruction->srcA.index, condition_table[instruction->srcA.index]);
				printf("\t LHS=%d, ",inst_log1->prev[0]);
				printf("IF goto label%04"PRIx32";\n", inst_log1->next[1]);
				if (err) {
					printf("IF CONDITION unknown\n");	
					return 1;
				}
				tmp = fprintf(fd, "IF goto ");
//				for (l = 0; l < inst_log1->next_size; l++) { 
//					tmp = fprintf(fd, ", label%04"PRIx32"", inst_log1->next[l]);
//				}
				tmp = fprintf(fd, "label%04"PRIx32";", inst_log1->next[1]);
				tmp = fprintf(fd, "\n");
				tmp = fprintf(fd, "\telse goto label%04"PRIx32";\n", inst_log1->next[0]);

				break;

			case NOP:
				if (print_inst(self, instruction, n, labels))
					return 1;
				break;
			case RET:
				if (print_inst(self, instruction, n, labels))
					return 1;
				printf("\t");
				tmp = fprintf(fd, "\t");
				printf("return\n");
				tmp = fprintf(fd, "return ");
				tmp = label_redirect[inst_log1->value1.value_id].redirect;
				label = &labels[tmp];
				tmp = output_label(label, fd);
				//tmp = fprintf(fd, " /*(0x%"PRIx64")*/", inst_log1->value1.value_id);
				tmp = fprintf(fd, ";\n");
				break;
			default:
				printf("Unhandled output instruction1 opcode=0x%x\n", instruction->opcode);
				tmp = fprintf(fd, "Unhandled output instruction\n");
				if (print_inst(self, instruction, n, labels))
					return 1;
				return 1;
				break;
			}
			if (0 < inst_log1->next_size && inst_log1->next[0] != (n + 1)) {		
				printf("\tTMP3 goto label%04"PRIx32";\n", inst_log1->next[0]);
				tmp = fprintf(fd, "\tTMP3 goto label%04"PRIx32";\n", inst_log1->next[0]);
			}
		}
	}
	if (0 < inst_log1->next_size && inst_log1->next[0]) {		
		printf("\tTMP1 goto label%04"PRIx32";\n", inst_log1->next[0]);
		tmp = fprintf(fd, "\tTMP1 goto label%04"PRIx32";\n", inst_log1->next[0]);
	}
	tmp = fprintf(fd, "}\n\n");
	return 0;
}

int register_label(struct external_entry_point_s *entry_point, uint64_t value_id,
	struct memory_s *value, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int n;
	int found;
	struct label_s *label;
	int label_offset;
	label_offset = label_redirect[value_id].redirect;
	label = &labels[label_offset];
	label->size_bits = value->length * 8;
	printf("Registering label: value_id = 0x%"PRIx64", scope 0x%"PRIx64", type 0x%"PRIx64", value 0x%"PRIx64", size 0x%"PRIx64", pointer 0x%"PRIx64", signed 0x%"PRIx64", unsigned 0x%"PRIx64"\n",
		value_id,
		label->scope,
		label->type,
		label->value,
		label->size_bits,
		label->lab_pointer,
		label->lab_signed,
		label->lab_unsigned);
	//int params_size;
	//int *params;
	//int *params_order;
	//int locals_size;
	//int *locals;
	//int *locals_order;
	found = 0;
	switch (label->scope) {
	case 2:
		printf("PARAM\n");
		for(n = 0; n < entry_point->params_size; n++) {
			printf("looping 0x%x\n", n);
			if (entry_point->params[n] == label_offset) {
				printf("Duplicate\n");
				found = 1;
				break;
			}
		}
		if (found) {
			break;
		}
		(entry_point->params_size)++;
		entry_point->params = realloc(entry_point->params, entry_point->params_size * sizeof(int));
		entry_point->params[entry_point->params_size - 1] = label_offset;
		break;
	case 1:
		printf("LOCAL\n");
		for(n = 0; n < entry_point->locals_size; n++) {
			printf("looping 0x%x\n", n);
			if (entry_point->locals[n] == label_offset) {
				printf("Duplicate\n");
				found = 1;
				break;
			}
		}
		if (found) {
			break;
		}
		(entry_point->locals_size)++;
		entry_point->locals = realloc(entry_point->locals, entry_point->locals_size * sizeof(int));
		entry_point->locals[entry_point->locals_size - 1] = label_offset;
		break;
	case 3:
		printf("HEX VALUE\n");
		break;
	default:
		printf("VALUE unhandled 0x%"PRIx64"\n", label->scope);
		break;
	}
	printf("params_size = 0x%x, locals_size = 0x%x\n",
		entry_point->params_size,
		entry_point->locals_size);

	printf("value: 0x%"PRIx64", 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64"\n",
		value->start_address,
		value->length,
		value->init_value,
		value->offset_value,
		value->value_type,
		value->value_scope,
		value->value_id);
	//tmp = register_label(label, &(inst_log1->value3));
	return 0;
}

int scan_for_labels_in_function_body(struct self_s *self, struct external_entry_point_s *entry_point,
			 int start, int end, struct label_redirect_s *label_redirect, struct label_s *labels)
{
	int tmp, n;
	int err;
	uint64_t value_id;
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;
	struct memory_s *value;
	struct label_s *label;

	if (!start || !end) {
		printf("scan_for_labels_in_function:Invalid start or end\n");
		return 1;
	}
	printf("scan_for_labels:start=0x%x, end=0x%x\n", start, end);

	for (n = start; n <= end; n++) {
		inst_log1 =  &inst_log_entry[n];
		if (!inst_log1) {
			printf("scan_for_labels:Invalid inst_log1[0x%x]\n", n);
			return 1;
		}

		instruction =  &inst_log1->instruction;

		/* Test to see if we have an instruction to output */
		printf("Inst 0x%04x: %d: value_type = %d, %d, %d\n", n,
			instruction->opcode,
			inst_log1->value1.value_type,
			inst_log1->value2.value_type,
			inst_log1->value3.value_type);
		if ((0 == inst_log1->value3.value_type) ||
			(1 == inst_log1->value3.value_type) ||
			(2 == inst_log1->value3.value_type) ||
			(3 == inst_log1->value3.value_type) ||
			(4 == inst_log1->value3.value_type) ||
			(6 == inst_log1->value3.value_type) ||
			(5 == inst_log1->value3.value_type)) {
			printf("Instruction Opcode = 0x%x\n", instruction->opcode);
			switch (instruction->opcode) {
			case MOV:
			case SEX:
				printf("SEX or MOV\n");
				if (inst_log1->value1.value_type == 6) {
					printf("ERROR1 %d\n", instruction->opcode);
					//break;
				}
				if (inst_log1->value1.value_type == 5) {
					printf("ERROR2\n");
					//break;
				}
				if (1 == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				if (1 == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);

				break;
			case ADD:
			case MUL:
			case IMUL:
			case SUB:
			case SBB:
			case rAND:
			case OR:
			case XOR:
			case NOT:
			case NEG:
			case SHL:
			case SHR:
			case SAL:
			case SAR:
				if (IND_MEM == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				printf("value3\n");
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				if (IND_MEM == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				printf("value1\n");
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;
			case JMP:
				break;
			case CALL:
				if (IND_MEM == instruction->dstA.indirect) {
					value_id = inst_log1->value3.indirect_value_id;
				} else {
					value_id = inst_log1->value3.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value3), label_redirect, labels);
				/* Special case for function pointers */
				if (IND_MEM == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
					tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				}
				break;
			case CMP:
			case TEST:
				if (IND_MEM == instruction->dstA.indirect) {
					value_id = inst_log1->value2.indirect_value_id;
				} else {
					value_id = inst_log1->value2.value_id;
				}
				printf("JCD6: Registering CMP label, value_id = 0x%"PRIx64"\n", value_id);
				tmp = register_label(entry_point, value_id, &(inst_log1->value2), label_redirect, labels);
				if (IND_MEM == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				printf("JCD6: Registering CMP label, value_id = 0x%"PRIx64"\n", value_id);
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;

			case IF:
				printf("IF: This might give signed or unsigned info to labels\n");
				break;

			case NOP:
				break;
			case RET:
				if (IND_MEM == instruction->srcA.indirect) {
					value_id = inst_log1->value1.indirect_value_id;
				} else {
					value_id = inst_log1->value1.value_id;
				}
				tmp = register_label(entry_point, value_id, &(inst_log1->value1), label_redirect, labels);
				break;
			default:
				printf("Unhandled scan instruction1\n");
				if (print_inst(self, instruction, n, labels))
					return 1;
				return 1;
				break;
			}
		}
	}
	return 0;
}
/***********************************************************************************
 * This is a complex routine. It utilises dynamic lists in order to reduce 
 * memory usage.
 **********************************************************************************/
int search_back_local_reg_stack(struct self_s *self, uint64_t mid_start_size, struct mid_start_s *mid_start, int reg_stack, uint64_t indirect_init_value, uint64_t indirect_offset_value, uint64_t *size, uint64_t **inst_list)
{
	struct instruction_s *instruction;
	struct inst_log_entry_s *inst_log1;
	struct inst_log_entry_s *inst_log_entry = self->inst_log_entry;
	uint64_t value_id;
	uint64_t inst_num;
	uint64_t tmp;
	int found = 0;
	int n;

	*size = 0;
	/* FIXME: This could be optimized out if the "seen" value just increased on each call */
	for (n = 0; n < INST_LOG_ENTRY_SIZE; n++) {
		search_back_seen[n] = 0;
	}

	printf("search_back_local_stack: 0x%"PRIx64", 0x%"PRIx64"\n", indirect_init_value, indirect_offset_value);
	if (0 < mid_start_size) {
		printf("search_back:prev_size=0x%"PRIx64"\n", mid_start_size);
	}
	if (0 == mid_start_size) {
		printf("search_back ended\n");
		return 1;
	}

	do {
		found = 0;
		for(n = 0; n < mid_start_size; n++) {
			if (1 == mid_start[n].valid) {
				inst_num = mid_start[n].mid_start;
				mid_start[n].valid = 0;
				found = 1;
				printf("mid_start removed 0x%"PRIx64" at 0x%x, size=0x%"PRIx64"\n", mid_start[n].mid_start, n, mid_start_size);
				break;
			}
		}
		if (!found) {
			printf("mid_start not found, exiting\n");
			goto search_back_exit_free;
		}
		if (search_back_seen[inst_num]) {
			continue;
		}
		search_back_seen[inst_num] = 1;
		inst_log1 =  &inst_log_entry[inst_num];
		instruction =  &inst_log1->instruction;
		value_id = inst_log1->value3.value_id;
		printf("inst_num:0x%"PRIx64"\n", inst_num);
		/* STACK */
		if ((reg_stack == 2) &&
			(instruction->dstA.store == STORE_REG) &&
			(inst_log1->value3.value_scope == 2) &&
			(instruction->dstA.indirect == IND_STACK) &&
			(inst_log1->value3.indirect_init_value == indirect_init_value) &&
			(inst_log1->value3.indirect_offset_value == indirect_offset_value)) {
			tmp = *size;
			tmp++;
			*size = tmp;
			if (tmp == 1) {
				*inst_list = malloc(sizeof(*inst_list));
				(*inst_list)[0] = inst_num;
			} else {
				*inst_list = realloc(*inst_list, tmp * sizeof(*inst_list));
				(*inst_list)[tmp - 1] = inst_num;
			}
		/* REGISTER */
		} else if ((reg_stack == 1) &&
			(instruction->dstA.store == STORE_REG) &&
			(instruction->dstA.indirect == IND_DIRECT) &&
			(instruction->dstA.index == indirect_init_value)) {
			tmp = *size;
			tmp++;
			*size = tmp;
			if (tmp == 1) {
				*inst_list = malloc(sizeof(*inst_list));
				(*inst_list)[0] = inst_num;
				printf("JCD2: inst_list[0] = 0x%"PRIx64"\n", inst_num);
			} else {
				*inst_list = realloc(*inst_list, tmp * sizeof(*inst_list));
				(*inst_list)[tmp - 1] = inst_num;
			}
		} else {
			if ((inst_log1->prev_size > 0) &&
				(inst_log1->prev[0] != 0)) {
				int prev_index;
				found = 0;
				prev_index = 0;
				for(n = 0; n < mid_start_size; n++) {
					if (0 == mid_start[n].valid) {
						mid_start[n].mid_start = inst_log1->prev[prev_index];
						prev_index++;
						mid_start[n].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[n].mid_start, n);
						found = 1;
					}
					if (prev_index >= inst_log1->prev_size) {
						break;
					}
				}
				if (prev_index < inst_log1->prev_size) {
					uint64_t mid_next;
					mid_next = mid_start_size + inst_log1->prev_size - prev_index;
					mid_start = realloc(mid_start, mid_next * sizeof(struct mid_start_s));
					for(n = mid_start_size; n < mid_next; n++) {
						mid_start[n].mid_start = inst_log1->prev[prev_index];
						prev_index++;
						printf("mid_start realloc added 0x%"PRIx64" at 0x%x\n", mid_start[n].mid_start, n);
						mid_start[n].valid = 1;
					}
					mid_start_size = mid_next;
				}
				
				if (!found) {
					printf("not found\n");
					goto search_back_exit_free;
				}
			}
		}
	/* FIXME: There must be deterministic exit point */
	} while (1);
	printf("end of loop, exiting\n");

search_back_exit_free:
	free(mid_start);
	return 0;
	
}

int main(int argc, char *argv[])
{
	int n = 0;
//	uint64_t offset = 0;
//	int instruction_offset = 0;
//	int octets = 0;
//	int result;
	char *filename;
	uint32_t arch;
	uint64_t mach;
	FILE *fd;
	int tmp;
	int err;
	const char *file = "test.obj";
	size_t inst_size = 0;
//	uint64_t reloc_size = 0;
	int l, m;
	struct instruction_s *instruction;
//	struct instruction_s *instruction_prev;
	struct inst_log_entry_s *inst_log1;
//	struct inst_log_entry_s *inst_log1_prev;
	struct inst_log_entry_s *inst_exe;
	struct inst_log_entry_s *inst_log_entry;
//	struct memory_s *value;
	uint64_t inst_log_prev = 0;
	int param_present[100];
	int param_size[100];
	char *expression;
	int not_finished;
	struct memory_s *memory_text;
	struct memory_s *memory_stack;
	struct memory_s *memory_reg;
	struct memory_s *memory_data;
	int *memory_used;
	struct label_redirect_s *label_redirect;
	struct label_s *labels;
	disassembler_ftype disassemble_fn;
	struct relocation_s *relocations;
	struct external_entry_point_s *external_entry_points;
	struct control_flow_node_s *nodes;
	int nodes_size;
	struct path_s *paths;
	int paths_size = 20000;
	struct loop_s *loops;
	int loops_size = 2000;

	expression = malloc(1000); /* Buffer for if expressions */

	handle = bf_test_open_file(file);
	if (!handle) {
		printf("Failed to find or recognise file\n");
		return 1;
	}
	tmp = bf_get_arch_mach(handle, &arch, &mach);
	if ((arch != 9) ||
		(mach != 8)) {
		printf("File not the correct arch(0x%x) and mach(0x%"PRIx64")\n", arch, mach);
		return 1;
	}

	printf("symtab_size = %ld\n", handle->symtab_sz);
	for (l = 0; l < handle->symtab_sz; l++) {
		printf("%d\n", l);
		printf("type:0x%02x\n", handle->symtab[l]->flags);
		printf("name:%s\n", handle->symtab[l]->name);
		printf("value=0x%02"PRIx64"\n", handle->symtab[l]->value);
		printf("section=%p\n", handle->symtab[l]->section);
		printf("section name=%s\n", handle->symtab[l]->section->name);
		printf("section flags=0x%02x\n", handle->symtab[l]->section->flags);
		printf("section index=0x%02"PRIx32"\n", handle->symtab[l]->section->index);
		printf("section id=0x%02"PRIx32"\n", handle->symtab[l]->section->id);
	}

	printf("sectiontab_size = %ld\n", handle->section_sz);
	for (l = 0; l < handle->section_sz; l++) {
		printf("%d\n", l);
		printf("flags:0x%02x\n", handle->section[l]->flags);
		printf("name:%s\n", handle->section[l]->name);
		printf("index=0x%02"PRIx32"\n", handle->section[l]->index);
		printf("id=0x%02"PRIx32"\n", handle->section[l]->id);
		printf("sectio=%p\n", handle->section[l]);
	}


	printf("Setup ok\n");
	inst_size = bf_get_code_size(handle);
	inst = malloc(inst_size);
	bf_copy_code_section(handle, inst, inst_size);
	printf("dis:.text Data at %p, size=0x%"PRIx64"\n", inst, inst_size);
	for (n = 0; n < inst_size; n++) {
		printf(" 0x%02x", inst[n]);
	}
	printf("\n");

	data_size = bf_get_data_size(handle);
	data = malloc(data_size);
	inst_log_entry = calloc(INST_LOG_ENTRY_SIZE, sizeof(struct inst_log_entry_s));
	relocations =  calloc(RELOCATION_SIZE, sizeof(struct relocation_s));
	external_entry_points = calloc(EXTERNAL_ENTRY_POINTS_MAX, sizeof(struct external_entry_point_s));
	self = malloc(sizeof *self);
	printf("sizeof struct self_s = 0x%"PRIx64"\n", sizeof *self);
	self->data_size = data_size;
	self->data = data;
	self->inst_log_entry = inst_log_entry;
	self->relocations = relocations;
	self->external_entry_points = external_entry_points;
	nodes = calloc(1000, sizeof(struct control_flow_node_s));
	nodes_size = 0;
	
	/* valgrind does not know about bf_copy_data_section */
	memset(data, 0, data_size);
	bf_copy_data_section(handle, data, data_size);
	printf("dis:.data Data at %p, size=0x%"PRIx64"\n", data, data_size);
	for (n = 0; n < data_size; n++) {
		printf(" 0x%02x", data[n]);
	}
	printf("\n");

	bf_get_reloc_table_code_section(handle);
	printf("reloc_table_code_sz=0x%"PRIx64"\n", handle->reloc_table_code_sz);
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		printf("reloc_table_code:addr = 0x%"PRIx64", size = 0x%"PRIx64", value = 0x%"PRIx64", section_index = 0x%"PRIx64", section_name=%s, symbol_name=%s\n",
			handle->reloc_table_code[n].address,
			handle->reloc_table_code[n].size,
			handle->reloc_table_code[n].value,
			handle->reloc_table_code[n].section_index,
			handle->reloc_table_code[n].section_name,
			handle->reloc_table_code[n].symbol_name);
	}

	bf_get_reloc_table_data_section(handle);
	for (n = 0; n < handle->reloc_table_data_sz; n++) {
		printf("reloc_table_data:addr = 0x%"PRIx64", size = 0x%"PRIx64", section = 0x%"PRIx64"\n",
			handle->reloc_table_data[n].address,
			handle->reloc_table_data[n].size,
			handle->reloc_table_data[n].section_index);
	}
	
	printf("handle=%p\n", handle);
	
	printf("handle=%p\n", handle);
	init_disassemble_info(&disasm_info, stdout, (fprintf_ftype) fprintf);
	disasm_info.flavour = bfd_get_flavour(handle->bfd);
	disasm_info.arch = bfd_get_arch(handle->bfd);
	disasm_info.mach = bfd_get_mach(handle->bfd);
	disasm_info.disassembler_options = "intel";
	disasm_info.octets_per_byte = bfd_octets_per_byte(handle->bfd);
	disasm_info.skip_zeroes = 8;
	disasm_info.skip_zeroes_at_end = 3;
	disasm_info.disassembler_needs_relocs = 0;
	disasm_info.buffer_length = inst_size;
	disasm_info.buffer = inst;

	printf("disassemble_fn\n");
	disassemble_fn = disassembler(handle->bfd);
	self->disassemble_fn = disassemble_fn;
	printf("disassemble_fn done %p, %p\n", disassemble_fn, print_insn_i386);
	dis_instructions.bytes_used = 0;
	inst_exe = &inst_log_entry[0];
	/* Where should entry_point_list_length be initialised */
	entry_point_list_length = ENTRY_POINTS_SIZE;
	/* Print the symtab */
	printf("symtab_sz = %lu\n", handle->symtab_sz);
	if (handle->symtab_sz >= 100) {
		printf("symtab too big!!! EXITING\n");
		return 1;
	}
	n = 0;
	for (l = 0; l < handle->symtab_sz; l++) {
		size_t length;
		/* FIXME: value == 0 for the first function in the .o file. */
		/*        We need to be able to handle more than
		          one function per .o file. */
		printf("section_id = %d, section_index = %d, flags = 0x%04x, value = 0x%04"PRIx64"\n",
			handle->symtab[l]->section->id,
			handle->symtab[l]->section->index,
			handle->symtab[l]->flags,
			handle->symtab[l]->value);
		if ((handle->symtab[l]->flags & 0x8) ||
			(handle->symtab[l]->flags == 0)) {
			external_entry_points[n].valid = 1;
			/* 1: Public function entry point
			 * 2: Private function entry point
			 * 3: Private label entry point
			 */
			if (handle->symtab[l]->flags & 0x8) {
				external_entry_points[n].type = 1;
			} else {
				external_entry_points[n].type = 2;
			}
			external_entry_points[n].section_offset = l;
			external_entry_points[n].section_id = 
				handle->symtab[l]->section->id;
			external_entry_points[n].section_index = 
				handle->symtab[l]->section->index;
			external_entry_points[n].value = handle->symtab[l]->value;
			length = strlen(handle->symtab[l]->name);
			external_entry_points[n].name = malloc(length+1);
			strncpy(external_entry_points[n].name, handle->symtab[l]->name, length+1);
			external_entry_points[n].process_state.memory_text =
				calloc(MEMORY_TEXT_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_stack =
				calloc(MEMORY_STACK_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_reg =
				calloc(MEMORY_REG_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_data =
				calloc(MEMORY_DATA_SIZE, sizeof(struct memory_s));
			external_entry_points[n].process_state.memory_used =
				calloc(MEMORY_USED_SIZE, sizeof(int));
			memory_text = external_entry_points[n].process_state.memory_text;
			memory_stack = external_entry_points[n].process_state.memory_stack;
			memory_reg = external_entry_points[n].process_state.memory_reg;
			memory_data = external_entry_points[n].process_state.memory_data;
			memory_used = external_entry_points[n].process_state.memory_used;

			ram_init(memory_data);
			reg_init(memory_reg);
			stack_init(memory_stack);
			/* Set EIP entry point equal to symbol table entry point */
			//memory_reg[2].init_value = EIP_START;
			memory_reg[2].offset_value = external_entry_points[n].value;

			print_mem(memory_reg, 1);

			n++;
		}

	}
	printf("Number of functions = %d\n", n);
	for (n = 0; n < EXTERNAL_ENTRY_POINTS_MAX; n++) {
		if (external_entry_points[n].valid != 0) {
		printf("%d: type = %d, sect_offset = %d, sect_id = %d, sect_index = %d, &%s() = 0x%04"PRIx64"\n",
			n,
			external_entry_points[n].type,
			external_entry_points[n].section_offset,
			external_entry_points[n].section_id,
			external_entry_points[n].section_index,
			external_entry_points[n].name,
			external_entry_points[n].value);
		}
	}
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		int len, len1;

		len = strlen(handle->reloc_table_code[n].symbol_name);
		for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
			if (external_entry_points[l].valid != 0) {
				len1 = strlen(external_entry_points[l].name);
				if (len != len1) {
					continue;
				}
				tmp = strncmp(external_entry_points[l].name, handle->reloc_table_code[n].symbol_name, len);
				if (0 == tmp) {
					handle->reloc_table_code[n].external_functions_index = l;
					handle->reloc_table_code[n].type =
						external_entry_points[l].type;
				}
			}
		}
	}
	for (n = 0; n < handle->reloc_table_code_sz; n++) {
		printf("reloc_table_code:addr = 0x%"PRIx64", size = 0x%"PRIx64", type = %d, function_index = 0x%"PRIx64", section_name=%s, symbol_name=%s\n",
			handle->reloc_table_code[n].address,
			handle->reloc_table_code[n].size,
			handle->reloc_table_code[n].type,
			handle->reloc_table_code[n].external_functions_index,
			handle->reloc_table_code[n].section_name,
			handle->reloc_table_code[n].symbol_name);
	}
			
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		if ((external_entry_points[l].valid != 0) &&
			(external_entry_points[l].type == 1)) {  /* 1 == Implemented in this .o file */
			struct process_state_s *process_state;
			
			printf("Start function block: %s:0x%"PRIx64"\n", external_entry_points[l].name, external_entry_points[l].value);	
			process_state = &external_entry_points[l].process_state;
			memory_text = process_state->memory_text;
			memory_stack = process_state->memory_stack;
			memory_reg = process_state->memory_reg;
			memory_data = process_state->memory_data;
			memory_used = process_state->memory_used;
			external_entry_points[l].inst_log = inst_log;
			/* EIP is a parameter for process_block */
			/* Update EIP */
			//memory_reg[2].offset_value = 0;
			//inst_log_prev = 0;
			entry_point[0].used = 1;
			entry_point[0].esp_init_value = memory_reg[0].init_value;
			entry_point[0].esp_offset_value = memory_reg[0].offset_value;
			entry_point[0].ebp_init_value = memory_reg[1].init_value;
			entry_point[0].ebp_offset_value = memory_reg[1].offset_value;
			entry_point[0].eip_init_value = memory_reg[2].init_value;
			entry_point[0].eip_offset_value = memory_reg[2].offset_value;
			entry_point[0].previous_instuction = 0;
			entry_point_list_length = ENTRY_POINTS_SIZE;

			print_mem(memory_reg, 1);
			printf ("LOGS: inst_log = 0x%"PRIx64"\n", inst_log);
			do {
				not_finished = 0;
				for (n = 0; n < entry_point_list_length; n++ ) {
					/* EIP is a parameter for process_block */
					/* Update EIP */
					//printf("entry:%d\n",n);
					if (entry_point[n].used) {
						memory_reg[0].init_value = entry_point[n].esp_init_value;
						memory_reg[0].offset_value = entry_point[n].esp_offset_value;
						memory_reg[1].init_value = entry_point[n].ebp_init_value;
						memory_reg[1].offset_value = entry_point[n].ebp_offset_value;
						memory_reg[2].init_value = entry_point[n].eip_init_value;
						memory_reg[2].offset_value = entry_point[n].eip_offset_value;
						inst_log_prev = entry_point[n].previous_instuction;
						not_finished = 1;
						printf ("LOGS: EIPinit = 0x%"PRIx64"\n", memory_reg[2].init_value);
						printf ("LOGS: EIPoffset = 0x%"PRIx64"\n", memory_reg[2].offset_value);
						err = process_block(self, process_state, handle, inst_log_prev, entry_point_list_length, entry_point, inst_size);
						/* clear the entry after calling process_block */
						entry_point[n].used = 0;
						if (err) {
							printf("process_block failed\n");
							return err;
						}
					}
				}
			} while (not_finished);	
			external_entry_points[l].inst_log_end = inst_log - 1;
			printf ("LOGS: inst_log_end = 0x%"PRIx64"\n", inst_log);
		}
	}
/*
	if (entry_point_list_length > 0) {
		for (n = 0; n < entry_point_list_length; n++ ) {
			printf("eip = 0x%"PRIx64", prev_inst = 0x%"PRIx64"\n",
				entry_point[n].eip_offset_value,
				entry_point[n].previous_instuction);
		}
	}
*/
	//inst_log--;
	printf("Instructions=%"PRId64", entry_point_list_length=%"PRId64"\n",
		inst_log,
		entry_point_list_length);

	/* Correct inst_log to identify how many dis_instructions there have been */
	inst_log--;

	tmp = tidy_inst_log(self);
	tmp = build_control_flow_nodes(self, nodes, &nodes_size);
	tmp = print_control_flow_nodes(self, nodes, &nodes_size);
//	print_dis_instructions(self);
//	exit(1);

	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		if (external_entry_points[l].valid) {
			tmp = find_node_from_inst(self, nodes, &nodes_size, external_entry_points[l].inst_log);
			external_entry_points[l].start_node = tmp;
		}
	}

	paths = calloc(paths_size, sizeof(struct path_s));
	for (n = 0; n < paths_size; n++) {
		paths[n].path = calloc(1000, sizeof(int));
	}
	loops = calloc(loops_size, sizeof(struct loop_s));

	for (n = 0; n < loops_size; n++) {
		loops[n].list = calloc(1000, sizeof(int));
	}

	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
//	for (l = 21; l < 22; l++) {
//	for (l = 37; l < 38; l++) {
		if (external_entry_points[l].valid && external_entry_points[l].type == 1) {
			printf("Starting external entry point %d:%s\n", l, external_entry_points[l].name);
			int paths_used = 0;
			int loops_used = 0;
			for (n = 0; n < paths_size; n++) {
				paths[n].used = 0;
				paths[n].path_prev = 0;
				paths[n].path_prev_index = 0;
				paths[n].path_size = 0;
				paths[n].type = 0;
				paths[n].loop_head = 0;
			}
			for (n = 0; n < loops_size; n++) {
				loops[n].size = 0;
				loops[n].head = 0;
			}

			tmp = build_control_flow_paths(self, nodes, &nodes_size,
				paths, &paths_size, &paths_used, external_entry_points[l].start_node);

			printf("tmp = %d, PATHS used = %d\n", tmp, paths_used);
			tmp = print_control_flow_paths(self, paths, &paths_size);
//			exit(1);

			tmp = build_control_flow_loops(self, paths, &paths_size, loops, &loops_size);
			tmp = build_node_paths(self, nodes, &nodes_size, paths, &paths_size);
			tmp = build_node_dominance(self, nodes, &nodes_size);
			tmp = analyse_control_flow_loop_exits(self, nodes, &nodes_size, loops, &loops_size);

			external_entry_points[l].paths_size = paths_used;
			external_entry_points[l].paths = calloc(paths_used, sizeof(struct path_s));
			for (n = 0; n < paths_used; n++) {
				external_entry_points[l].paths[n].used = paths[n].used;
				external_entry_points[l].paths[n].path_prev = paths[n].path_prev;
				external_entry_points[l].paths[n].path_prev_index = paths[n].path_prev_index;
				external_entry_points[l].paths[n].path_size = paths[n].path_size;
				external_entry_points[l].paths[n].type = paths[n].type;
				external_entry_points[l].paths[n].loop_head = paths[n].loop_head;

				external_entry_points[l].paths[n].path = calloc(paths[n].path_size, sizeof(int));
				for (m = 0; m  < paths[n].path_size; m++) {
					external_entry_points[l].paths[n].path[m] = paths[n].path[m];
				}

			}
			for (n = 0; n < loops_size; n++) {
				if (loops[n].size != 0) {
					loops_used = n + 1;
				}
			}
			printf("loops_used = 0x%x\n", loops_used);
			external_entry_points[l].loops_size = loops_used;
			external_entry_points[l].loops = calloc(loops_used, sizeof(struct loop_s));
			for (n = 0; n < loops_used; n++) {
				external_entry_points[l].loops[n].head = loops[n].head;
				external_entry_points[l].loops[n].size = loops[n].size;
				external_entry_points[l].loops[n].list = calloc(loops[n].size, sizeof(int));
				for (m = 0; m  < loops[n].size; m++) {
					external_entry_points[l].loops[n].list[m] = loops[n].list[m];
				}
			}
		}
	}
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
//	for (l = 21; l < 22; l++) {
//	for (l = 37; l < 38; l++) {
		if (external_entry_points[l].valid) {
			tmp = external_entry_points[l].start_node;
			printf("External entry point %d: type=%d, name=%s inst_log=0x%lx, start_node=0x%x\n", l, external_entry_points[l].type, external_entry_points[l].name, external_entry_points[l].inst_log, tmp);
			tmp = print_control_flow_paths(self, external_entry_points[l].paths, &(external_entry_points[l].paths_size));
			tmp = print_control_flow_loops(self, external_entry_points[l].loops, &(external_entry_points[l].loops_size));
		}
	}
	tmp = print_control_flow_nodes(self, nodes, &nodes_size);

	/* FIXME */
	exit(0);



	print_dis_instructions(self);

	if (entry_point_list_length > 0) {
		for (n = 0; n < entry_point_list_length; n++ ) {
			if (entry_point[n].used) {
				printf("%d, eip = 0x%"PRIx64", prev_inst = 0x%"PRIx64"\n",
					entry_point[n].used,
					entry_point[n].eip_offset_value,
					entry_point[n].previous_instuction);
			}
		}
	}
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * This bit creates the labels table, ready for the next step.
	 ************************************************************/
	printf("Number of labels = 0x%x\n", local_counter);
	/* FIXME: +1 added as a result of running valgrind, but need a proper fix */
	label_redirect = calloc(local_counter + 1, sizeof(struct label_redirect_s));
	labels = calloc(local_counter + 1, sizeof(struct label_s));
	printf("JCD6: local_counter=%d\n", local_counter);
	labels[0].lab_pointer = 1; /* EIP */
	labels[1].lab_pointer = 1; /* ESP */
	labels[2].lab_pointer = 1; /* EBP */
	/* n <= inst_log verified to be correct limit */
	for (n = 1; n <= inst_log; n++) {
		struct label_s label;
		uint64_t value_id;
		uint64_t value_id2;
		uint64_t value_id3;

		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		printf("value to log_to_label:n = 0x%x: 0x%x, 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
				n,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id);

		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case SUB:
		case SBB:
		case MUL:
		case IMUL:
		case OR:
		case XOR:
		case rAND:
		case NOT:
		case NEG:
		case SHL:
		case SHR:
		case SAL:
		case SAR:
		case SEX:
			if (IND_MEM == instruction->dstA.indirect) {
				value_id3 = inst_log1->value3.indirect_value_id;
			} else {
				value_id3 = inst_log1->value3.value_id;
			}
			if (value_id3 > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			memset(&label, 0, sizeof(struct label_s));
			tmp = log_to_label(instruction->dstA.store,
				instruction->dstA.indirect,
				instruction->dstA.index,
				instruction->dstA.relocated,
				inst_log1->value3.value_scope,
				inst_log1->value3.value_id,
				inst_log1->value3.indirect_offset_value,
				inst_log1->value3.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value3 unknown label %x\n", n);
			}
			if (!tmp && value_id3 > 0) {
				label_redirect[value_id3].redirect = value_id3;
				labels[value_id3].scope = label.scope;
				labels[value_id3].type = label.type;
				labels[value_id3].lab_pointer += label.lab_pointer;
				labels[value_id3].value = label.value;
			}

			if (IND_MEM == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
			} else {
				value_id = inst_log1->value1.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			memset(&label, 0, sizeof(struct label_s));
			tmp = log_to_label(instruction->srcA.store,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value1 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].lab_pointer += label.lab_pointer;
				labels[value_id].value = label.value;
			}
			break;

		/* Specially handled because value3 is not assigned and writen to a destination. */
		case TEST:
		case CMP:
			if (IND_MEM == instruction->dstA.indirect) {
				value_id2 = inst_log1->value2.indirect_value_id;
			} else {
				value_id2 = inst_log1->value2.value_id;
			}
			if (value_id2 > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			memset(&label, 0, sizeof(struct label_s));
			tmp = log_to_label(instruction->dstA.store,
				instruction->dstA.indirect,
				instruction->dstA.index,
				instruction->dstA.relocated,
				inst_log1->value2.value_scope,
				inst_log1->value2.value_id,
				inst_log1->value2.indirect_offset_value,
				inst_log1->value2.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value3 unknown label %x\n", n);
			}
			if (!tmp && value_id2 > 0) {
				label_redirect[value_id2].redirect = value_id2;
				labels[value_id2].scope = label.scope;
				labels[value_id2].type = label.type;
				labels[value_id2].lab_pointer += label.lab_pointer;
				labels[value_id2].value = label.value;
			}

			if (IND_MEM == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
			} else {
				value_id = inst_log1->value1.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			memset(&label, 0, sizeof(struct label_s));
			tmp = log_to_label(instruction->srcA.store,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value1 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].lab_pointer += label.lab_pointer;
				labels[value_id].value = label.value;
			}
			break;

		case CALL:
			printf("SSA CALL inst_log 0x%x\n", n);
			if (IND_MEM == instruction->dstA.indirect) {
				value_id = inst_log1->value3.indirect_value_id;
			} else {
				value_id = inst_log1->value3.value_id;
			}
			if (value_id > local_counter) {
				printf("SSA Failed at inst_log 0x%x\n", n);
				return 1;
			}
			memset(&label, 0, sizeof(struct label_s));
			tmp = log_to_label(instruction->dstA.store,
				instruction->dstA.indirect,
				instruction->dstA.index,
				instruction->dstA.relocated,
				inst_log1->value3.value_scope,
				inst_log1->value3.value_id,
				inst_log1->value3.indirect_offset_value,
				inst_log1->value3.indirect_value_id,
				&label);
			if (tmp) {
				printf("Inst:0x, value3 unknown label %x\n", n);
			}
			if (!tmp && value_id > 0) {
				label_redirect[value_id].redirect = value_id;
				labels[value_id].scope = label.scope;
				labels[value_id].type = label.type;
				labels[value_id].lab_pointer += label.lab_pointer;
				labels[value_id].value = label.value;
			}

			if (IND_MEM == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
				if (value_id > local_counter) {
					printf("SSA Failed at inst_log 0x%x\n", n);
					return 1;
				}
				memset(&label, 0, sizeof(struct label_s));
				tmp = log_to_label(instruction->srcA.store,
					instruction->srcA.indirect,
					instruction->srcA.index,
					instruction->srcA.relocated,
					inst_log1->value1.value_scope,
					inst_log1->value1.value_id,
					inst_log1->value1.indirect_offset_value,
					inst_log1->value1.indirect_value_id,
					&label);
				if (tmp) {
					printf("Inst:0x, value1 unknown label %x\n", n);
				}
				if (!tmp && value_id > 0) {
					label_redirect[value_id].redirect = value_id;
					labels[value_id].scope = label.scope;
					labels[value_id].type = label.type;
					labels[value_id].lab_pointer += label.lab_pointer;
					labels[value_id].value = label.value;
				}
			}
			break;
		case IF:
		case RET:
		case JMP:
			break;
		default:
			printf("SSA1 failed for Inst:0x%x, OP 0x%x\n", n, instruction->opcode);
			return 1;
			break;
		}
	}
	for (n = 0; n < local_counter; n++) {
		printf("labels 0x%x: redirect=0x%"PRIx64", scope=0x%"PRIx64", type=0x%"PRIx64", lab_pointer=0x%"PRIx64", value=0x%"PRIx64"\n",
			n, label_redirect[n].redirect, labels[n].scope, labels[n].type, labels[n].lab_pointer, labels[n].value);
	}
	
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * It build bi-directional links to instruction operands.
	 * This section does work for local_reg case. FIXME
	 ************************************************************/
	for (n = 1; n < inst_log; n++) {
		uint64_t value_id;
		uint64_t value_id1;
		uint64_t value_id2;
		uint64_t size;
		uint64_t *inst_list;
		uint64_t mid_start_size;
		struct mid_start_s *mid_start;

		size = 0;
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		value_id2 = inst_log1->value2.value_id;
		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case MUL:
		case OR:
		case XOR:
		case rAND:
		case SHL:
		case SHR:
		case CMP:
		/* FIXME: TODO */
			value_id = label_redirect[value_id1].redirect;
			if ((1 == labels[value_id].scope) &&
				(1 == labels[value_id].type)) {
				printf("Found local_reg Inst:0x%x:value_id:0x%"PRIx64"\n", n, value_id1);
				if (0 == inst_log1->prev_size) {
					printf("search_back ended\n");
					return 1;
				}
				if (0 < inst_log1->prev_size) {
					mid_start = calloc(inst_log1->prev_size, sizeof(struct mid_start_s));
					mid_start_size = inst_log1->prev_size;
					for (l = 0; l < inst_log1->prev_size; l++) {
						mid_start[l].mid_start = inst_log1->prev[l];
						mid_start[l].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[l].mid_start, l);
					}
				}
				tmp = search_back_local_reg_stack(self, mid_start_size, mid_start, 1, inst_log1->instruction.srcA.index, 0, &size, &inst_list);
				if (tmp) {
					printf("SSA search_back Failed at inst_log 0x%x\n", n);
					return 1;
				}
			}
			printf("SSA inst:0x%x:size=0x%"PRIx64"\n", n, size);
			/* Renaming is only needed if there are more than one label present */
			if (size > 0) {
				uint64_t value_id_highest = value_id;
				inst_log1->value1.prev = calloc(size, sizeof(int *));
				inst_log1->value1.prev_size = size;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					inst_log1->value1.prev[l] = inst_list[l];
					inst_log_l->value3.next = realloc(inst_log_l->value3.next, (inst_log_l->value3.next_size + 1) * sizeof(inst_log_l->value3.next));
					inst_log_l->value3.next[inst_log_l->value3.next_size] =
						 inst_list[l];
					inst_log_l->value3.next_size++;
					if (label_redirect[inst_log_l->value3.value_id].redirect > value_id_highest) {
						value_id_highest = label_redirect[inst_log_l->value3.value_id].redirect;
					}
					printf("rel inst:0x%"PRIx64"\n", inst_list[l]);
				}
				printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
					label_redirect[value_id1].redirect,
					value_id_highest);
				label_redirect[value_id1].redirect =
					value_id_highest;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
						label_redirect[inst_log_l->value3.value_id].redirect,
						value_id_highest);
					label_redirect[inst_log_l->value3.value_id].redirect =
						value_id_highest;
				}
			}
			break;
		default:
			break;
		}
	}
	/************************************************************
	 * This section deals with correcting SSA for branches/joins.
	 * It build bi-directional links to instruction operands.
	 * This section does work for local_stack case.
	 ************************************************************/
	for (n = 1; n < inst_log; n++) {
		uint64_t value_id;
		uint64_t value_id1;
		uint64_t size;
		uint64_t *inst_list;
		uint64_t mid_start_size;
		struct mid_start_s *mid_start;

		size = 0;
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		
		if (value_id1 > local_counter) {
			printf("SSA Failed at inst_log 0x%x\n", n);
			return 1;
		}
		switch (instruction->opcode) {
		case MOV:
		case ADD:
		case ADC:
		case SUB:
		case SBB:
		case MUL:
		case IMUL:
		case OR:
		case XOR:
		case rAND:
		case NOT:
		case NEG:
		case SHL:
		case SHR:
		case SAL:
		case SAR:
		case CMP:
		case TEST:
		case SEX:
			value_id = label_redirect[value_id1].redirect;
			if ((1 == labels[value_id].scope) &&
				(2 == labels[value_id].type)) {
				printf("Found local_stack Inst:0x%x:value_id:0x%"PRIx64"\n", n, value_id1);
				if (0 == inst_log1->prev_size) {
					printf("search_back ended\n");
					return 1;
				}
				if (0 < inst_log1->prev_size) {
					mid_start = calloc(inst_log1->prev_size, sizeof(struct mid_start_s));
					mid_start_size = inst_log1->prev_size;
					for (l = 0; l < inst_log1->prev_size; l++) {
						mid_start[l].mid_start = inst_log1->prev[l];
						mid_start[l].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[l].mid_start, l);
					}
				}
				tmp = search_back_local_reg_stack(self, mid_start_size, mid_start, 2, inst_log1->value1.indirect_init_value, inst_log1->value1.indirect_offset_value, &size, &inst_list);
				if (tmp) {
					printf("SSA search_back Failed at inst_log 0x%x\n", n);
					return 1;
				}
			}
			printf("SSA inst:0x%x:size=0x%"PRIx64"\n", n, size);
			/* Renaming is only needed if there are more than one label present */
			if (size > 0) {
				uint64_t value_id_highest = value_id;
				inst_log1->value1.prev = calloc(size, sizeof(int *));
				inst_log1->value1.prev_size = size;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					inst_log1->value1.prev[l] = inst_list[l];
					inst_log_l->value3.next = realloc(inst_log_l->value3.next, (inst_log_l->value3.next_size + 1) * sizeof(inst_log_l->value3.next));
					inst_log_l->value3.next[inst_log_l->value3.next_size] =
						 inst_list[l];
					inst_log_l->value3.next_size++;
					if (label_redirect[inst_log_l->value3.value_id].redirect > value_id_highest) {
						value_id_highest = label_redirect[inst_log_l->value3.value_id].redirect;
					}
					printf("rel inst:0x%"PRIx64"\n", inst_list[l]);
				}
				printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
					label_redirect[value_id1].redirect,
					value_id_highest);
				label_redirect[value_id1].redirect =
					value_id_highest;
				for (l = 0; l < size; l++) {
					struct inst_log_entry_s *inst_log_l;
					inst_log_l = &inst_log_entry[inst_list[l]];
					printf("Renaming label 0x%"PRIx64" to 0x%"PRIx64"\n",
						label_redirect[inst_log_l->value3.value_id].redirect,
						value_id_highest);
					label_redirect[inst_log_l->value3.value_id].redirect =
						value_id_highest;
				}
			}
			break;
		case IF:
		case RET:
		case JMP:
			break;
		case CALL:
			//printf("SSA2 failed for inst:0x%x, CALL\n", n);
			//return 1;
			break;
		default:
			printf("SSA2 failed for inst:0x%x, OP 0x%x\n", n, instruction->opcode);
			return 1;
			break;
		/* FIXME: TODO */
		}
	}
	/********************************************************
	 * This section filters out duplicate param_reg entries.
         * from the labels table: FIXME: THIS IS NOT NEEDED NOW
	 ********************************************************/
#if 0
	for (n = 0; n < (local_counter - 1); n++) {
		int tmp1;
		tmp1 = label_redirect[n].redirect;
		printf("param_reg:scanning base label 0x%x\n", n);
		if ((tmp1 == n) &&
			(labels[tmp1].scope == 2) &&
			(labels[tmp1].type == 1)) {
			int tmp2;
			/* This is a param_stack */
			for (l = n + 1; l < local_counter; l++) {
				printf("param_reg:scanning label 0x%x\n", l);
				tmp2 = label_redirect[l].redirect;
				if ((tmp2 == n) &&
					(labels[tmp2].scope == 2) &&
					(labels[tmp2].type == 1) &&
					(labels[tmp1].value == labels[tmp2].value) ) {
					printf("param_stack:found duplicate\n");
					label_redirect[l].redirect = n;
				}
			}
		}
	}
#endif
	/***************************************************
	 * Register labels in order to print:
	 * 	Function params,
	 *	local vars.
	 ***************************************************/
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		if (external_entry_points[l].valid &&
			external_entry_points[l].type == 1) {
		tmp = scan_for_labels_in_function_body(self, &external_entry_points[l],
				external_entry_points[l].inst_log,
				external_entry_points[l].inst_log_end,
				label_redirect,
				labels);
		if (tmp) {
			printf("Unhandled scan instruction 0x%x\n", l);
			return 1;
		}

		/* Expected param order: %rdi, %rsi, %rdx, %rcx, %r08, %r09 
		                         0x40, 0x38, 0x18, 0x10, 0x50, 0x58, then stack */
		
		printf("scanned: params = 0x%x, locals = 0x%x\n",
			external_entry_points[l].params_size,
			external_entry_points[l].locals_size);
		}
	}

	/***************************************************
	 * This section sorts the external entry point params to the correct order
	 ***************************************************/
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		for (m = 0; m < REG_PARAMS_ORDER_MAX; m++) {
			struct label_s *label;
			for (n = 0; n < external_entry_points[l].params_size; n++) {
				uint64_t tmp_param;
				tmp = external_entry_points[l].params[n];
				printf("JCD5: labels 0x%x, params_size=%d\n", tmp, external_entry_points[l].params_size);
				if (tmp >= local_counter) {
					printf("Invalid entry point 0x%x, l=%d, m=%d, n=%d, params_size=%d\n",
						tmp, l, m, n, external_entry_points[l].params_size);
					return 0;
				}
				label = &labels[tmp];
				printf("JCD5: labels 0x%x\n", external_entry_points[l].params[n]);
				printf("JCD5: label=%p, l=%d, m=%d, n=%d\n", label, l, m, n);
				printf("reg_params_order = 0x%x,", reg_params_order[m]);
				printf(" label->value = 0x%"PRIx64"\n", label->value);
				if ((label->scope == 2) &&
					(label->type == 1) &&
					(label->value == reg_params_order[m])) {
					/* Swap params */
					/* FIXME: How to handle the case of params_size <= n or m */
					if (n != m) {
						printf("JCD4: swapping n=0x%x and m=0x%x\n", n, m);
						tmp = external_entry_points[l].params_size;
						if ((m >= tmp || n >= tmp)) { 
							external_entry_points[l].params_size++;
							external_entry_points[l].params =
								realloc(external_entry_points[l].params, external_entry_points[l].params_size * sizeof(int));
							/* FIXME: Need to get label right */
							external_entry_points[l].params[external_entry_points[l].params_size - 1] =
								local_counter;
							local_counter++;
						}
						tmp_param = external_entry_points[l].params[n];
						external_entry_points[l].params[n] =
							external_entry_points[l].params[m];
						external_entry_points[l].params[m] = tmp_param;
					}
				}
			}
		}
	}




	/***************************************************
	 * This section, PARAM, deals with converting
	 * function params to reference locals.
	 * e.g. Change local0011 = function(param_reg0040);
	 *      to     local0011 = function(local0009);
	 ***************************************************/
// FIXME: Working on this
	for (n = 1; n < inst_log; n++) {
		struct label_s *label;
		uint64_t value_id;
		uint64_t value_id1;
		uint64_t size;
		uint64_t *inst_list;
		struct extension_call_s *call;
		struct external_entry_point_s *external_entry_point;
		uint64_t mid_start_size;
		struct mid_start_s *mid_start;

		size = 0;
		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		value_id1 = inst_log1->value1.value_id;
		
		if (value_id1 > local_counter) {
			printf("PARAM Failed at inst_log 0x%x\n", n);
			return 1;
		}
		switch (instruction->opcode) {
		case CALL:
			printf("PRINTING INST CALL\n");
			tmp = print_inst(self, instruction, n, labels);
			external_entry_point = &external_entry_points[instruction->srcA.index];
			inst_log1->extension = calloc(1, sizeof(struct extension_call_s));
			call = inst_log1->extension;
			call->params_size = external_entry_point->params_size;
			/* FIXME: use struct in sizeof bit here */
			call->params = calloc(call->params_size, sizeof(int *));
			if (!call) {
				printf("PARAM failed for inst:0x%x, CALL. Out of memory\n", n);
				return 1;
			}
			printf("PARAM:call size=%x\n", call->params_size);
			printf("PARAM:params size=%x\n", external_entry_point->params_size);
			for (m = 0; m < external_entry_point->params_size; m++) {
				label = &labels[external_entry_point->params[m]];
				if (0 == inst_log1->prev_size) {
					printf("search_back ended\n");
					return 1;
				}
				if (0 < inst_log1->prev_size) {
					mid_start = calloc(inst_log1->prev_size, sizeof(struct mid_start_s));
					mid_start_size = inst_log1->prev_size;
					for (l = 0; l < inst_log1->prev_size; l++) {
						mid_start[l].mid_start = inst_log1->prev[l];
						mid_start[l].valid = 1;
						printf("mid_start added 0x%"PRIx64" at 0x%x\n", mid_start[l].mid_start, l);
					}
				}
				/* param_regXXX */
				if ((2 == label->scope) &&
					(1 == label->type)) {
					printf("PARAM: Searching for REG0x%"PRIx64":0x%"PRIx64" + label->value(0x%"PRIx64")\n", inst_log1->value1.init_value, inst_log1->value1.offset_value, label->value);
					tmp = search_back_local_reg_stack(self, mid_start_size, mid_start, 1, label->value, 0, &size, &inst_list);
					printf("search_backJCD1: tmp = %d\n", tmp);
				} else {
				/* param_stackXXX */
				/* SP value held in value1 */
					printf("PARAM: Searching for SP(0x%"PRIx64":0x%"PRIx64") + label->value(0x%"PRIx64") - 8\n", inst_log1->value1.init_value, inst_log1->value1.offset_value, label->value);
					tmp = search_back_local_reg_stack(self, mid_start_size, mid_start, 2, inst_log1->value1.init_value, inst_log1->value1.offset_value + label->value - 8, &size, &inst_list);
				/* FIXME: Some renaming of local vars will also be needed if size > 1 */
				}
				if (tmp) {
					printf("PARAM search_back Failed at inst_log 0x%x\n", n);
					return 1;
				}
				tmp = output_label(label, stdout);
				tmp = fprintf(stdout, ");\n");
				tmp = fprintf(stdout, "PARAM size = 0x%"PRIx64"\n", size);
				if (size > 1) {
					printf("number of param locals (0x%"PRIx64") found too big at instruction 0x%x\n", size, n);
//					return 1;
//					break;
				}
				if (size > 0) {
					for (l = 0; l < size; l++) {
						struct inst_log_entry_s *inst_log_l;
						inst_log_l = &inst_log_entry[inst_list[l]];
						call->params[m] = inst_log_l->value3.value_id;
						// FIXME: Check next line. Force value type to unknown.
						printf("JCD3: Setting value_type to 0, was 0x%x\n", inst_log_l->value3.value_type);
						if (6 == inst_log_l->value3.value_type) {	
							inst_log_l->value1.value_type = 3;
							inst_log_l->value3.value_type = 3;
						}
						printf("JCD1: Param = 0x%"PRIx64", inst_list[0x%x] = 0x%"PRIx64"\n",

							inst_log_l->value3.value_id,
							l,
							inst_list[l]);
						//tmp = label_redirect[inst_log_l->value3.value_id].redirect;
						//label = &labels[tmp];
						//tmp = output_label(label, stdout);
					}
				}
			}
			//printf("SSA2 failed for inst:0x%x, CALL\n", n);
			//return 1;
			break;

		default:
			break;
		}
	}

	/**************************************************
	 * This section deals with variable types, scanning forwards
	 * FIXME: Need to make this a little more intelligent
	 * It might fall over with complex loops and program flow.
	 * Maybe iterate up and down until no more changes need doing.
	 * Problem with iterations, is that it could suffer from bistable flips
	 * causing the iteration to never exit.
	 **************************************************/
	for (n = 1; n <= inst_log; n++) {
		struct label_s label;
		uint64_t value_id;
		uint64_t value_id3;

		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		printf("value to log_to_label:n = 0x%x: 0x%x, 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
				n,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id);

		switch (instruction->opcode) {
		case MOV:
			if (IND_MEM == instruction->dstA.indirect) {
				value_id3 = inst_log1->value3.indirect_value_id;
			} else {
				value_id3 = inst_log1->value3.value_id;
			}

			if (IND_MEM == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
			} else {
				value_id = inst_log1->value1.value_id;
			}

			if (labels[value_id3].lab_pointer != labels[value_id].lab_pointer) {
				labels[value_id3].lab_pointer += labels[value_id].lab_pointer;
				labels[value_id].lab_pointer = labels[value_id3].lab_pointer;
			}
			printf("JCD4: value_id = 0x%"PRIx64", lab_pointer = 0x%"PRIx64", value_id3 = 0x%"PRIx64", lab_pointer = 0x%"PRIx64"\n",
				value_id, labels[value_id].lab_pointer, value_id3, labels[value_id3].lab_pointer);
			break;

		default:
			break;
		}
	}

	/**************************************************
	 * This section deals with variable types, scanning backwards
	 **************************************************/
	for (n = inst_log; n > 0; n--) {
		struct label_s label;
		uint64_t value_id;
		uint64_t value_id3;

		inst_log1 =  &inst_log_entry[n];
		instruction =  &inst_log1->instruction;
		printf("value to log_to_label:n = 0x%x: 0x%x, 0x%"PRIx64", 0x%x, 0x%x, 0x%"PRIx64", 0x%"PRIx64", 0x%"PRIx64"\n",
				n,
				instruction->srcA.indirect,
				instruction->srcA.index,
				instruction->srcA.relocated,
				inst_log1->value1.value_scope,
				inst_log1->value1.value_id,
				inst_log1->value1.indirect_offset_value,
				inst_log1->value1.indirect_value_id);

		switch (instruction->opcode) {
		case MOV:
			if (IND_MEM == instruction->dstA.indirect) {
				value_id3 = inst_log1->value3.indirect_value_id;
			} else {
				value_id3 = inst_log1->value3.value_id;
			}

			if (IND_MEM == instruction->srcA.indirect) {
				value_id = inst_log1->value1.indirect_value_id;
			} else {
				value_id = inst_log1->value1.value_id;
			}

			if (labels[value_id3].lab_pointer != labels[value_id].lab_pointer) {
				labels[value_id3].lab_pointer += labels[value_id].lab_pointer;
				labels[value_id].lab_pointer = labels[value_id3].lab_pointer;
			}
			printf("JCD4: value_id = 0x%"PRIx64", lab_pointer = 0x%"PRIx64", value_id3 = 0x%"PRIx64", lab_pointer = 0x%"PRIx64"\n",
				value_id, labels[value_id].lab_pointer, value_id3, labels[value_id3].lab_pointer);
			break;

		default:
			break;
		}
	}

	/***************************************************
	 * This section deals with outputting the .c file.
	 ***************************************************/
	filename = "test.c";
	fd = fopen(filename, "w");
	if (!fd) {
		printf("Failed to open file %s, error=%p\n", filename, fd);
		return 1;
	}
	printf(".c fd=%p\n", fd);
	printf("writing out to file\n");
	tmp = fprintf(fd, "#include <stdint.h>\n\n");
	printf("\nPRINTING MEMORY_DATA\n");
	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		struct process_state_s *process_state;
		if (external_entry_points[l].valid) {
			process_state = &external_entry_points[l].process_state;
			memory_data = process_state->memory_data;
			for (n = 0; n < 4; n++) {
				printf("memory_data:0x%x: 0x%"PRIx64"\n", n, memory_data[n].valid);
				if (memory_data[n].valid) {
	
					tmp = relocated_data(handle, memory_data[n].start_address, 4);
					if (tmp) {
						printf("int *data%04"PRIx64" = &data%04"PRIx64"\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
						tmp = fprintf(fd, "int *data%04"PRIx64" = &data%04"PRIx64";\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
					} else {
						printf("int data%04"PRIx64" = 0x%04"PRIx64"\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
						tmp = fprintf(fd, "int data%04"PRIx64" = 0x%"PRIx64";\n",
							memory_data[n].start_address,
							memory_data[n].init_value);
					}
				}
			}
		}
	}
	tmp = fprintf(fd, "\n");
	printf("\n");
#if 0
	for (n = 0; n < 100; n++) {
		param_present[n] = 0;
	}
		
	for (n = 0; n < 10; n++) {
		if (memory_stack[n].start_address > 0x10000) {
			uint64_t present_index;
			present_index = memory_stack[n].start_address - 0x10000;
			if (present_index >= 100) {
				printf("param limit reached:memory_stack[%d].start_address == 0x%"PRIx64"\n",
					n, memory_stack[n].start_address);
				continue;
			}
			param_present[present_index] = 1;
			param_size[present_index] = memory_stack[n].length;
		}
	}
	for (n = 0; n < 100; n++) {
		if (param_present[n]) {
			printf("param%04x\n", n);
			tmp = param_size[n];
			n += tmp;
		}
	}
#endif

	for (l = 0; l < EXTERNAL_ENTRY_POINTS_MAX; l++) {
		/* FIXME: value == 0 for the first function in the .o file. */
		/*        We need to be able to handle more than
		          one function per .o file. */
		if (external_entry_points[l].valid) {
			printf("%d:%s:start=%"PRIu64", end=%"PRIu64"\n", l,
					external_entry_points[l].name,
					external_entry_points[l].inst_log,
					external_entry_points[l].inst_log_end);
		}
		if (external_entry_points[l].valid &&
			external_entry_points[l].type == 1) {
			struct process_state_s *process_state;
			int tmp_state;
			
			process_state = &external_entry_points[l].process_state;

			tmp = fprintf(fd, "\n");
			output_function_name(fd, &external_entry_points[l]);
			tmp_state = 0;
			for (m = 0; m < REG_PARAMS_ORDER_MAX; m++) {
				struct label_s *label;
				for (n = 0; n < external_entry_points[l].params_size; n++) {
					label = &labels[external_entry_points[l].params[n]];
					printf("reg_params_order = 0x%x, label->value = 0x%"PRIx64"\n", reg_params_order[m], label->value);
					if ((label->scope == 2) &&
						(label->type == 1) &&
						(label->value == reg_params_order[m])) {
						if (tmp_state > 0) {
							fprintf(fd, ", ");
						}
						fprintf(fd, "int%"PRId64"_t ",
							label->size_bits);
						if (label->lab_pointer) {
							fprintf(fd, "*");
						}
						tmp = output_label(label, fd);
						tmp_state++;
					}
				}
			}
			for (n = 0; n < external_entry_points[l].params_size; n++) {
				struct label_s *label;
				label = &labels[external_entry_points[l].params[n]];
				if ((label->scope == 2) &&
					(label->type == 1)) {
					continue;
				}
				if (tmp_state > 0) {
					fprintf(fd, ", ");
				}
				fprintf(fd, "int%"PRId64"_t ",
					label->size_bits);
				if (label->lab_pointer) {
					fprintf(fd, "*");
				}
				tmp = output_label(label, fd);
				tmp_state++;
			}
			tmp = fprintf(fd, ")\n{\n");
			for (n = 0; n < external_entry_points[l].locals_size; n++) {
				struct label_s *label;
				label = &labels[external_entry_points[l].locals[n]];
				fprintf(fd, "\tint%"PRId64"_t ",
					label->size_bits);
				if (label->lab_pointer) {
					fprintf(fd, "*");
				}
				tmp = output_label(label, fd);
				fprintf(fd, ";\n");
			}
			fprintf(fd, "\n");
					
			tmp = output_function_body(self, process_state,
				fd,
				external_entry_points[l].inst_log,
				external_entry_points[l].inst_log_end,
				label_redirect,
				labels);
			if (tmp) {
				return 1;
			}
//   This code is not doing anything, so comment it out
//			for (n = external_entry_points[l].inst_log; n <= external_entry_points[l].inst_log_end; n++) {
//			}			
		}
	}

	fclose(fd);
	bf_test_close_file(handle);
	print_mem(memory_reg, 1);
	for (n = 0; n < inst_size; n++) {
		printf("0x%04x: %d\n", n, memory_used[n]);
	}
	printf("\nPRINTING MEMORY_DATA\n");
	for (n = 0; n < 4; n++) {
		print_mem(memory_data, n);
		printf("\n");
	}
	printf("\nPRINTING STACK_DATA\n");
	for (n = 0; n < 10; n++) {
		print_mem(memory_stack, n);
		printf("\n");
	}
	for (n = 0; n < 100; n++) {
		param_present[n] = 0;
	}
		
	for (n = 0; n < 10; n++) {
		if (memory_stack[n].start_address >= tmp) {
			uint64_t present_index;
			present_index = memory_stack[n].start_address - 0x10000;
			if (present_index >= 100) {
				printf("param limit reached:memory_stack[%d].start_address == 0x%"PRIx64"\n",
					n, memory_stack[n].start_address);
				continue;
			}
			param_present[present_index] = 1;
			param_size[present_index] = memory_stack[n].length;
		}
	}

	for (n = 0; n < 100; n++) {
		if (param_present[n]) {
			printf("param%04x\n", n);
			tmp = param_size[n];
			n += tmp;
		}
	}
	printf("END - FINISHED PROCESSING\n");
	return 0;
}

