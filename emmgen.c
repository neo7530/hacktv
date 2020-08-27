#include <stdio.h>

int main(int argc, char const *argv[]) {


    unsigned int tmparray[73] = {0};
		
	unsigned char _15[30], _fd[30], _02[30], _ea[30], _9b[30];
	unsigned char _49[30], _a1[30], _5e[30], _b6[30], _73[30];
	unsigned char _xor;
    char xx[800];
	bool start = 0;
	bool error = 0;
	
   	int idx = 0;
   	int i = 0;
   	FILE *fp = fopen(argv[1], "rt");
	FILE *emm = fopen(".emm.txt", "wt");

   	if (fp == NULL) {
       	fprintf(stderr, "Can't read emm.txt\n\n");
       	return 0;
   	}

	while(1 == 1){

		fgets(xx,400,fp);

		for(idx = 0;idx < 28;idx++){
			sscanf(&xx[idx*5],"0x%02x,",&tmparray[idx]);
		}

/*
	if(tmparray[4] == 0x73){
		start = 1;
		fprintf(stderr,"START!");
	}
*/	
//	if(start){
		switch(tmparray[4]){
			case 0x15:
				for(int i = 0;i<28;i++){
					_15[i] = tmparray[i];
				} break;
			case 0xfd:
				for(int i = 0;i<28;i++){
					_fd[i] = tmparray[i];
				} break;
			case 0x02:
				for(int i = 0;i<28;i++){
					_02[i] = tmparray[i];
				} break;
			case 0xea:
				for(int i = 0;i<28;i++){
					_ea[i] = tmparray[i];
				} break;
			case 0x49:
				for(int i = 0;i<28;i++){
					_49[i] = tmparray[i];
				} break;
			case 0xa1:
				for(int i = 0;i<28;i++){
					_a1[i] = tmparray[i];
				} break;
			case 0x5e:
				for(int i = 0;i<28;i++){
					_5e[i] = tmparray[i];
				} break;
			case 0xb6:
				for(int i = 0;i<28;i++){
					_b6[i] = tmparray[i];
				} break;
			case 0x73:
				for(int i = 0;i<28;i++){
					_73[i] = tmparray[i];
				} break;
			case 0x9b:
				for(int i = 0;i<28;i++){
					_9b[i] = tmparray[i];
				} break;
			default:
				break;
		}

	
	
    _xor = 0;
	
	for(int i=5;i<26;i++){
		_xor = _02[i] ^ _15[i] ^ _49[i] ^ _5e[i] ^ _73[i] ^ _9b[i] ^ _a1[i] ^ _b6[i] ^ _ea[i] ^ _fd[i];
	}


if(!_xor){
	for(int i=17;i<26;i++)fprintf(emm,"0x%02x,",_15[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_fd[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_02[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_ea[i]);	
	fprintf(emm,"\t /* Provider: %02x */ \n",_49[8]);
	for(int i=17;i<26;i++)fprintf(emm,"0x%02x,",_49[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_a1[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_5e[i]);
	for(int i=5;i<26;i++)fprintf(emm,"0x%02x,",_b6[i]);
	fprintf(emm,"\t /* Provider: %02x */ \n",_49[9]);	
}	
	
//	}

	
		if(feof(fp))break;

	}

	fclose(fp);
	fclose(emm);
	return 0;
}
