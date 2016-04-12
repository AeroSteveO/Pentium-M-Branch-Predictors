// my_predictor.h
// This file contains a sample gshare_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.
#include <iostream>


class gshare_update : public branch_update {
	public:
	unsigned int index;
};

class gshare_predictor : public branch_predictor {
	#define HISTORY_LENGTH	15
	#define TABLE_BITS	15
	
	public:
	gshare_update u;
	branch_info bi;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];
	
	gshare_predictor (void) : history(0) { 
		memset (tab, 0, sizeof (tab));
	}
	
	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
			(history << (TABLE_BITS - HISTORY_LENGTH)) 
			^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
			} else {
			u.direction_prediction (true);
		}
		u.target_prediction (0);
		return &u;
	}
	
	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((gshare_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
				} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
	}
};

//
// Pentium M hybrid branch predictors
// This class implements a simple hybrid branch predictor based on the Pentium M branch outcome prediction units. 
// Instead of implementing the complete Pentium M branch outcome predictors, the class below implements a hybrid 
// predictor that combines a bimodal predictor and a global predictor. 
class pm_update : public branch_update {
	public:
	unsigned index;
	unsigned short int tag;
	bool usedGlobal;
};
/*
	*	Global Predictor Structure
	*	This holds:
	*		2 bit predictor (pred)
	*		tag
	*		lastUsed Counter (lastused)
*/
struct GlobalPred {
	char lastused;
	char pred;
	unsigned short int tag;
};

class pm_predictor : public branch_predictor {
	#define GLOBAL_WAYS 4       // Ways in the global predictor
	#define GLOBAL_ROWS 512     // Rows in the global predictor
	//	#define HISTORY_LENGTH	15  // Gshare history length
	
	public:
	char bimodal[4096];    // Bimodal predictor table
	branch_info bi;        // Branch info for the current branch
	pm_update u;           // Branch update info
	unsigned bimodal_mask; // Bimodal binary mask to get the index for the predictor
	unsigned index_mask;   // Binary mask for the index portion of the hash
	unsigned tag_mask;     // Binary mask for the tag portion of the hash
	unsigned int history;  // Global history (PIR)
	
	// Binary masks for use in the PIR update function
	unsigned pir_update_mask;
	unsigned ip_pir_update_mask;
	unsigned pir_mask;
	
	// Binary masks for use in the hash function
	unsigned hash_ip_mask_a;
	unsigned hash_ip_mask_b;
	unsigned hash_ip_mask_c;
	unsigned hash_pir_mask_a;
	unsigned hash_pir_mask_b;
	unsigned hash_taken_mask;
	
	
	GlobalPred global_predictor[GLOBAL_WAYS][GLOBAL_ROWS];
	
	pm_predictor (void) { // CONSTRUCTOR
		for (int i = 0; i <4096; i++) { // INITIALIZE BIMODAL TABLE TO ALL 1's
			bimodal[i] = 1;
		}
		for (int i = 0; i < GLOBAL_WAYS; i++) { // INITIALIZE ALL GLOBAL PREDICTOR LAST USED TO 0
			for (int j = 0; j < GLOBAL_ROWS; j++) {
				global_predictor[i][j].lastused = 0;
				global_predictor[i][j].pred = 0;
				global_predictor[i][j].tag = 0;
			}
		}
		// Create general masks
		index_mask = createMask(6,14);
		bimodal_mask = createMask(0,11);
		tag_mask = createMask(0,5);
		
		// Create PIR masks
		pir_update_mask = createMask(0,12);
		ip_pir_update_mask = createMask(4,18);
		pir_mask = createMask(0,14);
		
		// Create Hash masks
		hash_ip_mask_a = createMask(13,18);
		hash_ip_mask_b = createMask(4,12);
		hash_ip_mask_c  = createMask(10,18);
		hash_pir_mask_a = createMask(0,5);
		hash_pir_mask_b = createMask(6,14);
		hash_taken_mask = createMask(0,5);
	} // END OF CONSTRUCTOR
	
	char getPred(unsigned short int tag, unsigned index) {
		assert(index < GLOBAL_ROWS); // if the index value is greater than the number of rows, something has seriously gone wrong
		
		for (int i = 0; i < GLOBAL_WAYS; i++) {
			if (global_predictor[i][index].tag == tag) {
				//std::cout << "GLOBAL HIT" << std::endl;
				//std::cout << (int) global_predictor[i][index].pred << std::endl;
				return global_predictor[i][index].pred;
			}
		}
		//std::cout << "GLOBAL MISS" << std::endl;
		return 6;
	}
	
	unsigned createMask(unsigned a, unsigned b) {
		unsigned r = 0;
		for (unsigned i=a; i<=b; i++)
		r |= 1 << i;
		return r;
	}
	
	branch_update *predict (branch_info & b) {
		bi = b;
		unsigned result = bimodal_mask & b.address;
		assert(result < 4096); // if the index value is greater than the number of rows, something has seriously gone wrong
		// predict branch outcome
		if (b.br_flags & BR_CONDITIONAL) {
			unsigned  hash = ((((b.address & hash_ip_mask_a)>>13) ^ (history & hash_pir_mask_a) )<<9) + (((b.address & hash_ip_mask_b)>>4)^((history & hash_pir_mask_b)>>6));
			//u.index = (history) ^ (b.address); // Gshare stuff			
			
			u.index = (index_mask & hash) >> 6;
			u.tag = tag_mask & hash;
			char globalPred = getPred(u.tag, u.index);
			
			if (globalPred < 5) { // USING THE GLOBAL PREDICTOR
				u.usedGlobal = true;
				if (globalPred >= 2) {
					u.direction_prediction(true);
				}
				else if (globalPred <= 1) {
					u.direction_prediction(false);
				}
			} // END USING THE GLOBAL PREDICTOR
			else {
				u.usedGlobal = false;
				if (bimodal[result] >= 2) {
					u.direction_prediction(true);
				}
				else if (bimodal[result] <= 1) {
					u.direction_prediction(false);
				}
			}
		}
		else {
			u.usedGlobal = false;
			u.direction_prediction(true);
		}
		
	    // TARGET PREDICTION
		u.target_prediction (0);
		
		return &u;
	} // END OF BRANCH PREDICT
	
	void update (branch_update *u, bool taken, unsigned int target) {
		
		history = ((history & pir_update_mask) << 2 ) ^ ((BR_CONDITIONAL*(bi.address & ip_pir_update_mask)) | (BR_INDIRECT*((bi.address & hash_ip_mask_c >> 5) + (target & hash_taken_mask))));
		
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned index = ((pm_update*)u)->index;
			unsigned short int tag = ((pm_update*)u)->tag;
			unsigned result = bi.address & bimodal_mask;
			
			if (taken) {
				if (bimodal[result] < 3) {
					bimodal[result]++;
				}
			}
			else {
				if (bimodal[result] > 0) {
					bimodal[result]--;
				}
			}
			
 			//history <<= 1;
			//history |= taken;
			//history &= (1<<HISTORY_LENGTH)-1;
			//history = ((history & pir_update_mask) << 2 ) ^ ((bi.address & ip_pir_update_mask)>>4);
			
			//std::cout << tag << std::endl;
			assert(index < GLOBAL_ROWS);
			assert(tag < 64);
			// if we used the global predictor, we'll update the global predictor
			if (((pm_update*)u)->usedGlobal) {
				for (int i = 0; i < GLOBAL_WAYS; i++) {
					if (global_predictor[i][index].tag == tag) {
						if (taken){
							if (global_predictor[i][index].pred < 3){
								global_predictor[i][index].pred++;
							}
						}
						else {
							if (global_predictor[i][index].pred > 0) {
								global_predictor[i][index].pred--;
							}
						}
						if (global_predictor[i][index].lastused < 5) {
							global_predictor[i][index].lastused++;
						}
						break;
					}
				}
			}
			else { // if not, we'll yell at the global predictor, reducing its last used, and add in a new predictor if possible
				bool swap = false;
				for (int i = 0; i < GLOBAL_WAYS; i++) {
					if (global_predictor[i][index].lastused > 0) {
						global_predictor[i][index].lastused--;
					}
					else if (global_predictor[i][index].lastused <= 0 && !swap) {  // lastUsed is zero, use it
						global_predictor[i][index].tag = tag;
						global_predictor[i][index].pred = bimodal[result];
						global_predictor[i][index].lastused++;
						swap=true;
					}
				}
			}
		}
	}
};

//
// Complete Pentium M branch predictors for extra credit
// This class implements the complete Pentium M branch prediction units. 
// It implements both branch target prediction and branch outcome predicton. 
class cpm_update : public branch_update {
	public:
	unsigned int index;
};

class cpm_predictor : public branch_predictor {
	public:
	cpm_update u;
	
	cpm_predictor (void) {
	}
	
	branch_update *predict (branch_info & b) {
		u.direction_prediction (true);
		u.target_prediction (0);
		return &u;
	}
	
	void update (branch_update *u, bool taken, unsigned int target) {
	}
	
};


