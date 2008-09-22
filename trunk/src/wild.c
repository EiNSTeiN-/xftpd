
#ifdef WIN32
#include <windows.h>
#else
#include <string.h>
#endif


#define upchar(c) (((c>='a') && (c<='z'))?(c - 32):(c))

//return the count of wildchars until the end of the string
static int wildchar_count(const char* wild) {
	int i,count=0;

	for(i=0;i<strlen(wild);i++) if(wild[i] == '*') count++;

	return count;
}

//return the size until the next wildchar or until the end
static int len_to_wild_or_end(const char* wild) {
	int i;

	for(i=0;i<strlen(wild);i++) if(wild[i] == '*') return i;

	return i;
}


// compare case-insensively a string with wildchars to a plain string
int strcasewildcmp(const char *wild, const char *plain) {
	int w,p;

	// if 'plain' is wilded we won't be able to do anything
	if(wildchar_count(plain) > 0) return 1;

	for(w=0,p=0;w<strlen(wild);w++) {
		// check if we have a wildchar
		if(wild[w] == '*') {
			// check if this wildchar is the last char
			if(wild[w+1] == 0) return 0;

			// if this is the last wildchar so compare directly from the end
			if(wildchar_count(&wild[w+1]) == 0) {
				// check if we don't cross the end of 'plain'
				if(len_to_wild_or_end(&plain[p])<len_to_wild_or_end(&wild[w+1])) return 1;

				// if the last part is equal then return
				if(!strcasecmp(&plain[strlen(plain)-len_to_wild_or_end(&wild[w+1])],&wild[w+1])) return 0;
				else return 1;
			} else {
				// there is another wild char out there
				// if we have another wildchar next to this one, continue with the next
				if(len_to_wild_or_end(&wild[w+1]) == 0) continue;

				// loop to check if the string after this wildcard can be found in 'plain'
				for(;;p++) {

					// check if we don't cross the end of 'plain'
					if(len_to_wild_or_end(&plain[p])<len_to_wild_or_end(&wild[w+1])) return 1;

					// if this part is equal so break and continue
					if(!strncasecmp(&plain[p],&wild[w+1],len_to_wild_or_end(&wild[w+1]))) break;
				}
			}
		} else {
			// check if we reached the end of the plain string
			if(plain[p] == 0) return 1;

			// if we don't have the same char in 'wild' than 
			// in 'plain' then we exit
			if(upchar(wild[w]) != upchar(plain[p])) return 1;

			// else continue with the next char in 'plain'
			p++;
		}
	}

	// we reached the end of 'wild' so
	// check if the same goes for 'plain'
	if(plain[p] == 0) return 0;

	// if it's not the case, fail
	return 1;
}
