
// filter_games.cpp

// includes

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "filter_games.h"
#include "move.h"
#include "move_do.h"
#include "move_legal.h"
#include "pgn.h"
#include "san.h"
#include "util.h"

// constants

static const int COUNT_MAX = 16384;

static const int NIL = -1;

// types

struct entry_t {
   uint64 key;
   uint16 move;
   uint16 n;
   uint16 sum;
   uint16 colour;
};

struct book_t {
   int size;
   int alloc;
   uint32 mask;
   entry_t * entry;
   sint32 * hash;
};

// variables

static int MaxPly;
static int MinGame;
static double MinScore;
static bool RemoveWhite, RemoveBlack;
static bool Uniform;

static book_t Book[1];

// prototypes

static void   book_clear    ();
static void   book_insert   (const char file_name[]);
static void   book_filter   (const char file_name[]);

static int    find_entry    (const board_t * board, int move, bool create);
static void   resize        ();
static void   halve_stats   (uint64 key);



// functions

// filter_games()

void filter_games(int argc, char * argv[]) {

   int i;
   const char * forbidden_pgn_file;
   const char * input_pgn_file;
   const char * bin_file;

   forbidden_pgn_file = NULL;
   input_pgn_file = NULL;

   bin_file = NULL;
   my_string_set(&bin_file,"book.bin");

   MaxPly = 1024;

   for (i = 1; i < argc; i++) {

      if (false) {

      } else if (my_string_equal(argv[i],"filter-games")) {

         // skip

      } else if (my_string_equal(argv[i],"-forbidden-pgn")) {

         i++;
         if (argv[i] == NULL) my_fatal("filter_games(): missing argument\n");

         my_string_set(&forbidden_pgn_file,argv[i]);

      } else if (my_string_equal(argv[i],"-input-pgn")) {

         i++;
         if (argv[i] == NULL) my_fatal("filter_games(): missing argument\n");

         my_string_set(&input_pgn_file,argv[i]);

      } else if (my_string_equal(argv[i],"-bin")) {

         i++;
         if (argv[i] == NULL) my_fatal("filter_games(): missing argument\n");

         my_string_set(&bin_file,argv[i]);

      } else {

         my_fatal("filter_games(): unknown option \"%s\"\n",argv[i]);
      }
   }

   book_clear();

   fputs("learning forbidden games ...\n", stderr);
   book_insert(forbidden_pgn_file);

   fputs("filtering games ...\n", stderr);
   book_filter(input_pgn_file);

   fputs("all done!\n", stderr);
}

// book_clear()

static void book_clear() {

   int index;

   Book->alloc = 1;
   Book->mask = (Book->alloc * 2) - 1;

   Book->entry = (entry_t *) my_malloc(Book->alloc*sizeof(entry_t));
   Book->size = 0;

   Book->hash = (sint32 *) my_malloc((Book->alloc*2)*sizeof(sint32));
   for (index = 0; index < Book->alloc*2; index++) {
      Book->hash[index] = NIL;
   }
}

// book_insert()

static void book_insert(const char file_name[]) {

   int game_nb;
   pgn_t pgn[1];
   board_t board[1];
   int ply;
   int result;
   char string[256];
   int move;
   int pos;

   ASSERT(file_name!=NULL);

   // init

   game_nb = 0;

   // scan loop

   pgn_open(pgn,file_name);

   while (pgn_next_game(pgn)) {

      board_start(board);
      ply = 0;
      result = 0;

      if (false) {
      } else if (my_string_equal(pgn->result,"1-0")) {
         result = +1;
      } else if (my_string_equal(pgn->result,"0-1")) {
         result = -1;
      }

      while (pgn_next_move(pgn,string,256)) {

         if (ply < MaxPly) {

            move = move_from_san(string,board);

            if (move == MoveNone || !move_is_legal(move,board)) {
              fprintf(stderr, "book_insert(): illegal move \"%s\" at line %d, column %d\n",string,pgn->move_line,pgn->move_column);
               continue;
            }

            pos = find_entry(board,move,true);

            Book->entry[pos].n++;
            Book->entry[pos].sum += result+1;

            if (Book->entry[pos].n >= COUNT_MAX) {
               halve_stats(board->key);
            }

            move_do(board,move);
            ply++;
            result = -result;
         }
      }

      game_nb++;
      if (game_nb % 10000 == 0) fprintf(stderr,"%d games ...\n",game_nb);
   }

   pgn_close(pgn);

   fprintf(stderr, "%d game%s.\n",game_nb,(game_nb>1)?"s":"");
   fprintf(stderr, "%d entries.\n",Book->size);

   return;
}



// find_entry()

static int find_entry(const board_t * board, int move, bool create) {

   uint64 key;
   int index;
   int pos;

   ASSERT(board!=NULL);
   ASSERT(move_is_ok(move));

   ASSERT(move_is_legal(move,board));

   // init

   key = board->key;

   // search

   for (index = key & Book->mask; (pos=Book->hash[index]) != NIL; index = (index+1) & Book->mask) {

      ASSERT(pos>=0&&pos<Book->size);

      if (Book->entry[pos].key == key && Book->entry[pos].move == move) {
         return pos; // found
      }
   }

   if (!create) {
     return -1;
   }

   // not found

   ASSERT(Book->size<=Book->alloc);

   if (Book->size == Book->alloc) {

      // allocate more memory

      resize();

      for (index = key & Book->mask; Book->hash[index] != NIL; index = (index+1) & Book->mask)
         ;
   }

   // create a new entry

   ASSERT(Book->size<Book->alloc);
   pos = Book->size++;

   Book->entry[pos].key = key;
   Book->entry[pos].move = move;
   Book->entry[pos].n = 0;
   Book->entry[pos].sum = 0;
   Book->entry[pos].colour = board->turn;

   // insert into the hash table

   ASSERT(index>=0&&index<Book->alloc*2);
   ASSERT(Book->hash[index]==NIL);
   Book->hash[index] = pos;

   ASSERT(pos>=0&&pos<Book->size);

   return pos;
}

// resize()

static void resize() {

   int size;
   int pos;
   int index;

   ASSERT(Book->size==Book->alloc);

   Book->alloc *= 2;
   Book->mask = (Book->alloc * 2) - 1;

   size = 0;
   size += Book->alloc * sizeof(entry_t);
   size += (Book->alloc*2) * sizeof(sint32);

   if (size >= 1048576) printf("allocating %gMB ...\n",double(size)/1048576.0);

   // resize arrays

   Book->entry = (entry_t *) my_realloc(Book->entry,Book->alloc*sizeof(entry_t));
   Book->hash = (sint32 *) my_realloc(Book->hash,(Book->alloc*2)*sizeof(sint32));

   // rebuild hash table

   for (index = 0; index < Book->alloc*2; index++) {
      Book->hash[index] = NIL;
   }

   for (pos = 0; pos < Book->size; pos++) {

      for (index = Book->entry[pos].key & Book->mask; Book->hash[index] != NIL; index = (index+1) & Book->mask)
         ;

      ASSERT(index>=0&&index<Book->alloc*2);
      Book->hash[index] = pos;
   }
}

// halve_stats()

static void halve_stats(uint64 key) {

   int index;
   int pos;

   // search

   for (index = key & Book->mask; (pos=Book->hash[index]) != NIL; index = (index+1) & Book->mask) {

      ASSERT(pos>=0&&pos<Book->size);

      if (Book->entry[pos].key == key) {
         Book->entry[pos].n = (Book->entry[pos].n + 1) / 2;
         Book->entry[pos].sum = (Book->entry[pos].sum + 1) / 2;
      }
   }
}


static void book_filter(const char file_name[]) {
   int game_nb;
   pgn_t pgn[1];
   board_t board[1];
   int ply;
   int result;
   char string[256];
   int move;
   int pos;
   int num_OK;
   bool still_in_book;

   ASSERT(file_name!=NULL);

   // init

   game_nb = 0;
   num_OK = 0;

   // scan loop

   pgn_open(pgn,file_name);

   while (pgn_next_game(pgn)) {
      board_start(board);
      ply = 0;

      still_in_book = true;
      while (pgn_next_move(pgn,string,256)) {
         if (ply < MaxPly) {
            move = move_from_san(string,board);

            if (move == MoveNone || !move_is_legal(move,board)) {
               printf("book_filter(): illegal move \"%s\" at line %d, column %d\n",string,pgn->move_line,pgn->move_column);
               continue;
            }
            pos = find_entry(board,move,false);
            if (pos == -1) {
              still_in_book = false;
            }
            move_do(board,move);
            ply++;
         }
      }
      if (!still_in_book) {
        num_OK++;
        printf("%s",(char *)(pgn->game_string));
      }
      game_nb++;
      if (game_nb % 10000 == 0) fprintf(stderr,"%d games, %d OK ...\n",game_nb,num_OK);
   }
   pgn_close(pgn);
   fprintf(stderr, "ALL DONE.  %d games, %d OK ...\n",game_nb,num_OK);
}


// end of filter_games.cpp

