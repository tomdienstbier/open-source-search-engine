#include "gb-include.h"

#include "Words.h"
#include "Phrases.h" // for isInPhrase() for hashWordIffNotInPhrase
#include "Unicode.h" // getUtf8CharSize()
#include "StopWords.h"
#include "Speller.h"
#include "HashTableX.h"
#include "Sections.h"
#include "XmlNode.h" // getTagLen()

//static int32_t printstring ( char *s , int32_t len ) ;

Words::Words ( ) {
	m_buf = NULL;
	m_bufSize = 0;
	reset();
}
Words::~Words ( ) {
	reset();
}
void Words::reset ( ) {
	m_numWords = 0;
	m_numAlnumWords = 0;
	m_xml = NULL;
	m_preCount = 0;
	if ( m_buf && m_buf != m_localBuf && m_buf != m_localBuf2 )
		mfree ( m_buf , m_bufSize , "Words" );
	m_buf = NULL;
	m_bufSize = 0;
	m_nodes = NULL;
	m_tagIds = NULL;
	m_numTags = 0;
	m_hasTags = false;
	m_localBuf2 = NULL;
	m_localBufSize2 = 0;
}

bool Words::set( char *s, int32_t slen, bool computeWordIds, int32_t niceness ) {
	// bail if nothing
	if ( ! s || slen == 0 ) {
		m_numWords = 0;
		m_numAlnumWords = 0;
		return true;
	}

	char c = s[slen];
	if ( c != '\0' ) {
		s[slen] = '\0';
	}

	bool status = set( s, computeWordIds, niceness );
	if ( c != '\0' ) {
		s[slen] = c;
	}

	return status;
}

// a quickie
// this url gives a m_preCount that is too low. why?
// http://go.tfol.com/163/speed.asp
int32_t countWords ( char *p , int32_t plen ) {
	char *pend  = p + plen;
	int32_t  count = 1;

	while ( p < pend ) {
		// sequence of punct
		for  ( ; p < pend && ! is_alnum_utf8 (p) ; p += getUtf8CharSize(p) ) {
			// in case being set from xml tags, count as words now
			if ( *p=='<') count++; 
		}
		count++;

		// sequence of alnum
		for  ( ; p < pend && is_alnum_utf8 (p) ; p += getUtf8CharSize(p) )
			;

		count++;

	};
	// some extra for good meaure
	return count+10;
}

int32_t countWords ( char *p ) {
	int32_t  count = 1;

	while ( *p ) {
		// sequence of punct
		for  ( ; *p && ! is_alnum_utf8 (p) ; p += getUtf8CharSize(p) ) {
			// in case being set from xml tags, count as words now
			if ( *p=='<') count++; 
		}
		count++;

		// sequence of alnum
		for  ( ; *p && is_alnum_utf8 (p) ; p += getUtf8CharSize(p) )
			;

		count++;

	}
	// some extra for good meaure
	return count+10;
}

bool Words::set( Xml *xml, bool computeWordIds, int32_t niceness, int32_t node1, int32_t node2 ) {
	// prevent setting with the same string
	if ( m_xml == xml ) { char *xx=NULL;*xx=0; }
	reset();
	m_xml = xml;

	// if xml is empty, bail
	if ( !xml->getContent() ) {
		return true;
	}

	int32_t numNodes = xml->getNumNodes();
	if ( numNodes <= 0 ) {
		return true;
	}

	// . can be given a range, if node2 is -1 that means all!
	// . range is half-open: [node1, node2)
	if ( node2 < 0 ) {
		node2 = numNodes;
	}

	// sanity check
	if ( node1 > node2 ) { char *xx=NULL;*xx=0; }

	char *start = xml->getNode(node1);
	char *end = xml->getNode( node2 - 1 ) + xml->getNodeLen( node2 - 1 );
	int32_t  size  = end - start;

	m_preCount = countWords( start , size );

	// allocate based on the approximate count
	if ( !allocateWordBuffers( m_preCount, true ) ) {
		return false;
	}

	// are we done?
	for ( int32_t k = node1; k < node2 && m_numWords < m_preCount; ++k ) {
		// get the kth node
		char *node = xml->getNode( k );
		int32_t nodeLen = xml->getNodeLen( k );

		// is the kth node a tag?
		if ( !xml->isTag( k ) ) {
			/// @todo ALC why are we adding NULL and restoring it after?
			/// addWords should be change to use nodeLen and not null terminated string
			char c = node[nodeLen];
			node[nodeLen] = '\0';
			addWords( node, nodeLen, computeWordIds, niceness );
			node[nodeLen] = c;
			continue;
		}

		// it is a tag
		m_words    [m_numWords] = node;
		m_wordLens [m_numWords] = nodeLen;
		m_tagIds   [m_numWords] = xml->getNodeId(k);
		m_wordIds  [m_numWords] = 0LL;
		m_nodes    [m_numWords] = k;

		// we have less than 127 HTML tags, so set 
		// the high bit for back tags
		if ( xml->isBackTag(k)) {
			m_tagIds[m_numWords] |= BACKBIT;
		}

		//log(LOG_DEBUG, "Words: Word %"INT32": got tag %s%s (%d)", 
		//    m_numWords,
		//    isBackTag(m_numWords)?"/":"",
		//    g_nodes[getTagId(m_numWords)].m_nodeName,
		//    getTagId(m_numWords));
		
		m_numWords++;

		// used by XmlDoc.cpp
		m_numTags++;

		continue;
	}

	return true;
}

bool Words::set11 ( char *s , char *send , int32_t niceness ) {
	reset();

	// this will make addWords() scan for tags
	m_hasTags = true;

	// save it
	char saved = *send;

	// null term
	*send = '\0';

	// determine rough upper bound on number of words by counting
	// punct/alnum boundaries
	m_preCount = countWords ( s );

	// true = tagIds
	bool status = allocateWordBuffers(m_preCount,true);

	// deal with error now
	if ( !status ) {
		*send = saved;
		return false;
	}

	// and set the words
	status = addWords(s,0x7fffffff, true, niceness );

	// bring it back
	*send = saved;

	// return error?
	return status;
}

// . set words from a string
// . assume no HTML entities in the string "s"
// . s must be NULL terminated
// . NOTE: do not free "s" from under us cuz we reference it
// . break up the string ,"s", into "words".
// . doesn't do tags, only text nodes in "xml"
// . our definition of a word is as close to English as we can get it
// . BUT we also consider a string of punctuation characters to be a word
bool Words::set( char *s, bool computeWordIds, int32_t niceness ) {
	reset();

	// determine rough upper bound on number of words by counting
	// punct/alnum boundaries
	m_preCount = countWords ( s );
	if ( !allocateWordBuffers( m_preCount ) ) {
		return false;
	}

	return addWords( s, 0x7fffffff, computeWordIds, niceness );
}

#include "XmlNode.h"

bool Words::addWords( char *s, int32_t nodeLen, bool computeWordIds, int32_t niceness ) {
	int32_t  i = 0;
	int32_t  j;
	//int32_t  k = 0;
	int32_t  wlen;
	//uint32_t e;
	//int32_t  skip;
	int32_t badCount = 0;

	bool hadApostrophe = false;

	UCScript oldScript = ucScriptCommon;
	UCScript saved;
	UCProps props;

 uptop:

	// bad utf8 can cause a breach
	if ( i >= nodeLen ) {
		goto done;
	}

	if ( ! s[i] ) {
		goto done;
	}

	if ( !is_alnum_utf8( s + i ) ) {
		if ( m_numWords >= m_preCount ) {
			goto done;
		}

		// tag?
		if ( s[i]=='<' && m_hasTags && isTagStart(s+i) ) {
			// get the tag id
			if ( s[i + 1] == '/' ) {
				// skip over /
				m_tagIds[m_numWords] = ::getTagId( s + i + 2 );
				m_tagIds[m_numWords] |= BACKBIT;
			} else {
				m_tagIds[m_numWords] = ::getTagId( s + i + 1 );
			}

			m_words[m_numWords] = s + i;
			m_wordIds[m_numWords] = 0LL;

			// skip till end
			int32_t tagLen = getTagLen( s + i );
			m_wordLens[m_numWords] = tagLen;
			m_nodes[m_numWords] = 0;
			m_numWords++;

			// advance
			i += tagLen;
			goto uptop;
		}

		// it is a punct word, find end of it
		char *start = s+i;
		for ( ; s[i] ; i += getUtf8CharSize(s+i)) {
			// stop on < if we got tags
			if ( s[i] == '<' && m_hasTags ) {
				break;
			}

			// breathe
			QUICKPOLL(niceness);

			// if we are simple ascii, skip quickly
			if ( is_ascii(s[i]) ) {
				// accumulate NON-alnum chars
				if ( ! is_alnum_a(s[i]) ) {
					continue;
				}

				// update
				oldScript = ucScriptCommon;

				// otherwise, stop we got alnum
				break;
			}

			// if we are utf8 we stop on special props
			UChar32 c = utf8Decode ( s+i );

			// stop if word char
			if ( ! ucIsWordChar ( c ) ) {
				continue;
			}

			// update first though
			oldScript = ucGetScript ( c );

			// then stop
			break;
		}
		m_words        [ m_numWords  ] = start;
		m_wordLens     [ m_numWords  ] = s+i - start;
		m_wordIds      [ m_numWords  ] = 0LL;
		m_nodes        [ m_numWords  ] = 0;

		if (m_tagIds) {
			m_tagIds[m_numWords] = 0;
		}

		m_numWords++;
		goto uptop;
	}

	// get an alnum word
	j = i;
 again:
	//for ( ; is_alnum_utf8 (&s[i] ) ; i += getUtf8CharSize(s+i) );
	for ( ; s[i] ; i += getUtf8CharSize(s+i) ) {
		// breathe
		QUICKPOLL(niceness);
		// simple ascii?
		if ( is_ascii(s[i]) ) {
			// accumulate alnum chars
			if ( is_alnum_a(s[i]) ) continue;
			// update
			oldScript = ucScriptCommon;
			// otherwise, stop we got punct
			break;
		}
		// get the code point of the utf8 char
		UChar32 c = utf8Decode ( s+i );
		// get props
		props = ucProperties ( c );
		// good stuff?
		if ( props & (UC_IGNORABLE|UC_EXTEND) ) continue;
		// stop? if UC_WORCHAR is set, that means its an alnum
		if ( ! ( props & UC_WORDCHAR ) ) {
			// reset script between words
			oldScript = ucScriptCommon;
			break;
		}
		// save it
		saved = oldScript;
		// update here
		oldScript = ucGetScript(c);
		// treat ucScriptLatin (30) as common so we can have latin1
		// like char without breaking the word!
		if ( oldScript == ucScriptLatin ) oldScript = ucScriptCommon;
		// stop on this crap too i guess. like japanes chars?
		if ( props & ( UC_IDEOGRAPH | UC_HIRAGANA | UC_THAI ) ) {
			// include it
			i += getUtf8CharSize(s+i);
			// but stop
			break;
		}
		// script change?
		if ( saved != oldScript ) break;
	}
	
	// . java++, A++, C++ exception
	// . A+, C+, exception
	// . TODO: consider putting in Bits.cpp w/ D_CAN_BE_IN_PHRASE
	if ( s[i]=='+' ) {
		if ( s[i+1]=='+' && !is_alnum_utf8(&s[i+2]) ) i += 2;
		else if ( !is_alnum_utf8(&s[i+1]) ) i++;
	}
	// . c#, j#, ...
	if ( s[i]=='#' && !is_alnum_utf8(&s[i+1]) ) i++;

	// comma is ok if like ,ddd!d
	if ( s[i]==',' && 
	     i-j <= 3 &&
	     is_digit(s[i-1]) ) {
		// if word so far is 2 or 3 chars, make sure digits
		if ( i-j >= 2 && ! is_digit(s[i-2]) ) goto nogo;
		if ( i-j >= 3 && ! is_digit(s[i-3]) ) goto nogo;
		// scan forward
		while ( s[i] == ',' &&
		        is_digit(s[i+1]) &&
		        is_digit(s[i+2]) &&
		        is_digit(s[i+3]) &&
		        ! is_digit(s[i+4]) ) {
			i += 4;
		}
	}

	// decimal point?
	if ( s[i] == '.' &&
	     is_digit(s[i-1]) &&
	     is_digit(s[i+1]) ) {
		// allow the decimal point
		i++;
		// skip over string of digits
		while ( is_digit(s[i]) ) i++;
	}
	
 nogo:

	// allow for words like we're dave's and i'm
	if ( s[i] == '\'' && s[i + 1] && is_alnum_utf8( &s[i + 1] ) && !hadApostrophe ) {
		i++;
		hadApostrophe = true;
		goto again;
	}
	hadApostrophe = false;
	
	// get word length
	wlen = i - j;
	if ( m_numWords >= m_preCount ) goto done;
	m_words   [ m_numWords  ] = &s[j];
	m_wordLens[ m_numWords  ] = wlen;

	// . Lars says it's better to leave the accented chars intact
	// . google agrees
	// . but what about "re'sume"?
	if ( computeWordIds ) {
		int64_t h = hash64Lower_utf8(&s[j],wlen);
		m_wordIds [m_numWords] = h;

		// until we get an accent removal algo, comment this
		// out and possibly use the query synonym pipeline
		// to search without accents. MDW
		//int64_t h2 = hash64AsciiLowerE(&s[j],wlen);
		//if ( h2 != h ) m_stripWordIds [m_numWords] = h2;
		//else           m_stripWordIds [m_numWords] = 0LL;
		//m_stripWordIds[m_numWords] = 0;
	}
	m_nodes[m_numWords] = 0;
	if (m_tagIds) m_tagIds[m_numWords] = 0;
	m_numWords++;
	m_numAlnumWords++;
	// break on \0 or MAX_WORDS
	//if ( ! s[i] ) goto done;
	// get a punct word
	goto uptop;

 done:
	// bad programming warning
	if ( m_numWords > m_preCount ) {
		log(LOG_LOGIC, "build: words: set: Fix counting routine.");
		char *xx = NULL; *xx = 0;
	}
	// compute total length
	if ( m_numWords <= 0 ) {
		m_totalLen = 0;
	} else {
		m_totalLen = m_words[m_numWords-1] - s + m_wordLens[m_numWords-1];
	}

	if ( badCount )
		log("words: had %"INT32" bad utf8 chars",badCount);

	return true;
}

// common to Unicode and ISO-8859-1
bool Words::allocateWordBuffers(int32_t count, bool tagIds) {
	// alloc if we need to (added 4 more for m_nodes[])
	int32_t wordSize = 0;
	wordSize += sizeof(char *);
	wordSize += sizeof(int32_t);
	wordSize += sizeof(int64_t);
	wordSize += sizeof(int32_t);
	if ( tagIds ) wordSize += sizeof(nodeid_t);
	m_bufSize = wordSize * count;
	if(m_bufSize < 0) return log("build: word count overflow %"INT32" "
				     "bytes wordSize=%"INT32" count=%"INT32".",
				     m_bufSize, wordSize, count);
	if ( m_bufSize <= m_localBufSize2 && m_localBuf2 ) {
		m_buf = m_localBuf2;
	}
	else if ( m_bufSize <= WORDS_LOCALBUFSIZE ) {
		m_buf = m_localBuf;
	}
	else {
		m_buf = (char *)mmalloc ( m_bufSize , "Words" );
		if ( ! m_buf ) return log("build: Could not allocate %"INT32" "
					  "bytes for parsing document.",
					  m_bufSize);
	}

	// set ptrs
	char *p = m_buf;
	m_words    = (char     **)p ;
	p += sizeof(char*) * count;
	m_wordLens = (int32_t      *)p ;
	p += sizeof(int32_t)* count;
	m_wordIds  = (int64_t *)p ;
	p += sizeof (int64_t) * count;
	//m_stripWordIds  = (int64_t *)p ;
	//p += sizeof (int64_t) * count;
	m_nodes = (int32_t *)p;
	p += sizeof(int32_t) * count;

	if (tagIds) {
		m_tagIds = (nodeid_t*) p;
		p += sizeof(nodeid_t) * count;
	}

	if ( p > m_buf + m_bufSize ) { char *xx=NULL;*xx=0; }

	return true;
}

void Words::print( ) {
	for (int32_t i=0;i<m_numWords;i++) {
		printWord(i);
		printf("\n");
	}
}

void Words::printWord ( int32_t i ) {
	fprintf(stderr,"#%05"INT32" ",i);
	fprintf(stderr,"%020"UINT64" ",m_wordIds[i]);
	// print the word
	printstring(m_words[i],m_wordLens[i]);
	//if (m_spam.m_spam[i]!=0)
	//	printf("[%i]",m_spam.m_spam[i]);
}

int32_t printstring ( char *s , int32_t len ) {
	// filter out \n's and \r's
	int32_t olen = 0;
	for ( int32_t i = 0 ; i < len && olen < 17 ; i++ ) {
		if ( s[i] == '\n' || s[i] =='\r' ) continue;
		olen++;
		fprintf(stderr,"%c",s[i]);
	}
	if ( olen == 17 ) fprintf(stderr,"...");
	//while ( olen < 20 ) { fprintf(stderr," "); olen++; }
	return olen;
}

static uint8_t s_findMaxIndex(int64_t *array, int num, int *wantmax = NULL) {
	if(!array || num < 2 || num > 255) return(0);
	int64_t max, oldmax;
	int idx = 0;
	max = oldmax = INT_MIN;
	for(int x = 0; x < num; x++) {
		if(array[x] >= max) {
			oldmax = max;
			max = array[x];
			idx = x;
		}
	}
	if(max == 0) return(0);
	if(max == oldmax) return(0);
	if(wantmax) *wantmax = max;
	return((uint8_t)idx);
}

unsigned char Words::isBounded(int wordi) {
	if(wordi+1 < m_numWords &&
	   getWord(wordi)[getWordLen(wordi)] == '/' //||
	    //getWord(wordi)[getWordLen(wordi)] == '?'
	   )
		return(true);
	if(wordi+1 < m_numWords &&
	   (getWord(wordi)[getWordLen(wordi)] == '.' ||
	    getWord(wordi)[getWordLen(wordi)] == '?') &&
	   is_alnum_a(getWord(wordi)[getWordLen(wordi)+1]) )
		return(true);
	if(wordi > 0 &&
	   (getWord(wordi)[-1] == '/' ||
	    getWord(wordi)[-1] == '?'))
		return(true);

	return(false);
}

unsigned char getCharacterLanguage ( char *utf8Char ) {
	// romantic?
	char cs = getUtf8CharSize ( utf8Char );
	// can't say what language it is
	if ( cs == 1 ) return langUnknown;
	// convert to 32 bit unicode
	UChar32 c = utf8Decode ( utf8Char );
	UCScript us = ucGetScript ( c );
	// arabic? this also returns for persian!! fix?
	if ( us == ucScriptArabic ) 
		return langArabic;
	if ( us == ucScriptCyrillic )
		return langRussian;
	if ( us == ucScriptHebrew )
		return langHebrew;
	if ( us == ucScriptGreek )
		return langGreek;

	return langUnknown;
}

// returns -1 and sets g_errno on error, because 0 means langUnknown
int32_t Words::getLanguage( Sections *sections ,
			 int32_t maxSamples,
			 int32_t niceness,
			 int32_t *langScore) {

	// . take a random sample of words and look them up in the
	//   language dictionary
	//HashTableT<int64_t, char> ht;
	HashTableX ht;
	int64_t langCount[MAX_LANGUAGES];
	int64_t langWorkArea[MAX_LANGUAGES];
	int32_t numWords = m_numWords;
	//int32_t skip = numWords/maxSamples;
	//if ( skip == 0 ) skip = 1;
	// reset the language count
	memset(langCount, 0, sizeof(int64_t)*MAX_LANGUAGES);
	// sample the words
	//int32_t wordBase  = 0;
	int32_t wordi     = 0;
	//if ( ! ht.set(maxSamples*1.5) ) return -1;
	if ( ! ht.set(8,1,(int32_t)(maxSamples*8.0),NULL,0,false,
		      niceness,"wordslang")) 
		return -1;
 
	// . avoid words in these bad sections
	// . google seems to index SEC_MARQUEE so i took that out of badFlags
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT;
	// shortcuts
	int64_t *wids  = m_wordIds;
	int32_t      *wlens = m_wordLens;
	char     **wptrs = m_words;

	//int32_t langTotal = 0;
// 	log ( LOG_WARN, "xmldoc: Picking language from %"INT32" words with %"INT32" skip",
// 			numWords, skip );
	char numOne = 1;
	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;
	// this means null too
	if ( sections && sections->m_numSections == 0 ) sp = NULL;

	int32_t maxCount = 1000;

	while ( wordi < numWords ) {
		// breathe
		QUICKPOLL( niceness );
		// move to the next valid word
		if ( ! wids [wordi]     ) { wordi++; continue; }
		if (   wlens[wordi] < 2 ) { wordi++; continue; }
		// skip if in a bad section
		//int32_t flags = sections->m_sectionPtrs[i]->m_flags;
		// meaning script section ,etc
		if ( sp && ( sp[wordi]->m_flags & badFlags ) ) {
			wordi++; continue; }
		// check the language
		//unsigned char lang = 0;

		// Skip if word is capitalized and not preceded by a tag
		//if(s_isWordCap(getWord(wordi), getWordLen(wordi)) &&
		//   wordi > 0 && !getTagId(wordi - 1)) {
		//	wordi++;
		//	continue;
		//}

		// Skip word if bounded by '/' or '?' might be in a URL
		if(isBounded(wordi)) {
			wordi++;
			continue;
		}

		// is it arabic? sometimes they are spammy pages and repeat
		// a few arabic words over and over again, so don't do deduping
		// with "ht" before checking this.
		char cl = getCharacterLanguage ( wptrs[wordi] );
		if ( cl ) {
		        langCount[(unsigned char)cl]++;
			wordi++;
			continue;
		}

		//if(ht.getSlot(m_wordIds[wordi]) !=-1) {
		if(!ht.isEmpty(&m_wordIds[wordi]) ) {
			wordi++;
			continue;
		}

		// If we can't add the word, it's not that bad.
		// Just gripe about it in the log.
		if(!ht.addKey(&m_wordIds[wordi], &numOne)) {
			log(LOG_WARN, "build: Could not add word to temporary "
			    "table, memory error?\n");
			g_errno = ENOMEM;
			return -1;
		}

		if ( maxCount-- <= 0 ) break;

		// No lang from charset, got a phrase, and 0 language does not have 
		// a score Order is very important!
		int foundone = 0;
		if ( // lang == 0 &&
		    // we seem to be missing hungarian and thai
		    g_speller.getPhraseLanguages(getWord(wordi),
						 getWordLen(wordi), 
						 langWorkArea) &&
		    // why must it have an "unknown score" of 0?
		    // allow -1... i don't know what that means!!
		    langWorkArea[0] <= 0) {
			
			int lasty = -1;
			for(int y = 1; y < MAX_LANGUAGES; y++) {
				if(langWorkArea[y] == 0) continue;
				langCount[y]++;
				int32_t pop = langWorkArea[y];
				// negative means in an official dictionary
				if ( pop < 0 ) {
					pop *= -1;
					langCount[y] += 1;
				}
				// extra?
				if ( pop > 1000 )
					langCount[y] += 2;
				if ( pop > 10000 )
					langCount[y] += 2;
				lasty = y;
				foundone++;
			}
			// . if it can only belong to one language
			// . helps fix that fact that our unifiedDict is crummy
			//   and identifes some words as being in a lot of languages
			//   like "Pronto" as being in english and not giving
			//   the popularities correctly.
			if ( foundone == 1 )
				// give massive boost
				langCount[lasty] += 10;
		}
		// . try to skip unknown words without killing sample size
		// . we lack russian, hungarian and arabic in the unified
		//   dict, so try to do character detection for those langs.
		// . should prevent them from being detected as unknown
		//   langs and coming up for english search 'gigablast'
		if ( ! foundone ) {
			langCount[langUnknown]++;
			// do not count towards sample size
			maxCount++;
		}

		// skip to the next word
		//wordBase += skip;
		//if ( wordi < wordBase )
		//	wordi = wordBase;
		//else
		wordi++;
	}
	// punish unknown count in case a doc has a lot of proper names
	// or something
	//langCount[langUnknown] /= 2;
	// get the lang with the max score then
	int l = s_findMaxIndex(langCount, MAX_LANGUAGES);
	// if(langCount[l] < 15) return(langUnknown);
	if(langScore) *langScore = langCount[l];
	// return if known now
	return l;
}

// get the word index at the given character position 
int32_t Words::getWordAt ( char *p ) { // int32_t charPos ) {
	if ( ! p                  ) { char *xx=NULL;*xx=0; }
	if ( p <  m_words[0]      ) { char *xx=NULL;*xx=0; }
	if ( p >= getContentEnd() ) { char *xx=NULL;*xx=0; }
	
	int32_t step = m_numWords / 2;
	int32_t i = m_numWords / 2 ;

	for (;;) {
		// divide it by 2 each time
		step >>= 1;
		// always at least one
		if ( step <= 0 )
			step = 1;
		// is it a hit?
		if ( p >= m_words[i] && p < m_words[i] + m_wordLens[i] )
			return i;
		// compare
		if ( m_words[i] < p )
			i += step;
		else
			i -= step;
	}
	return -1;
}


// . return the value of the specified "field" within this html tag, "s"
// . the case of "field" does not matter
char *getFieldValue ( char *s , 
		      int32_t  slen ,
		      char *field , 
		      int32_t *valueLen ) {
	// reset this to 0
	*valueLen = 0;
	// scan for the field name in our node
	int32_t flen = gbstrlen(field);
	char inQuotes = '\0';
	int32_t i;

	// make it sane
	if ( slen > 2000 ) slen = 2000;

	for ( i = 1; i + flen < slen ; i++ ) {
		// skip the field if it's quoted
		if ( inQuotes) {
			if (s[i] == inQuotes ) inQuotes = 0;
			continue;
		}
		// set inQuotes to the quote if we're in quotes
		if ( (s[i]=='\"' || s[i]=='\'')){ 
			inQuotes = s[i];
			continue;
		} 
		// if not in quote tag might end
		if ( s[i] == '>' && ! inQuotes ) return NULL;
		// a field name must be preceeded by non-alnum
		if ( is_alnum_a ( s[i-1] ) ) continue;
		// the first character of this field shout match field[0]
		if ( to_lower_a (s[i]) != to_lower_a(field[0] )) continue;
		// field just be immediately followed by an = or space
		if (s[i+flen]!='='&&!is_wspace_a(s[i+flen]))continue;
		// field names must match
		if ( strncasecmp ( &s[i], field, flen ) != 0 ) continue;
		// break cuz we got a match for our field name
		break;
	}
	
	
	// return NULL if no matching field
	if ( i + flen >= slen ) return NULL;

	// advance i over the fieldname so it pts to = or space
	i += flen;

	// advance i over spaces
	while ( i < slen && is_wspace_a ( s[i] ) ) i++;

	// advance over the equal sign, return NULL if does not exist
	if ( i < slen && s[i++] != '=' ) return NULL;

	// advance i over spaces after the equal sign
	while ( i < slen && is_wspace_a ( s[i] ) ) i++;
	
	// now parse out the value of this field (could be in quotes)
	inQuotes = '\0';

	// set inQuotes to the quote if we're in quotes
	if ( s[i]=='\"' || s[i]=='\'') inQuotes = s[i++]; 

	// mark this as the start of the value
	int start=i;

	// advance i until we hit a space, or we hit a that quote if inQuotes
	if (inQuotes) while (i<slen && s[i] != inQuotes ) i++;
	else while ( i<slen &&!is_wspace_a(s[i])&&s[i]!='>')i++;

	// set the length of the value
	*valueLen = i - start;

	// return a ptr to the value
	return s + start;
}
