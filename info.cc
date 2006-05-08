/* A class for dealing with .NFO files */

#include <ctype.h>
#include <fstream>

#include "info.h"
#include "sprites.h"
#include "error.h"

const char *infoline = "%s %d %d %02X %d %d %d %d";
//		<PCX-File> <X> <Y> <info[0..7]>
//		extended if info[0]&8: info[1]*<%d %d> linelen linestart


int makeint(U8 low, S8 high)
{
  S16 combined;

  combined = (high << 8) | low;
  return combined;
}

void read_file(istream&in,int infover,AllocArray<Sprite>&sprites);

inforeader::inforeader(char *fn)
{
  ifstream f;
  f.open(fn);
  
  string buffer;
  int infover;

  pcx = NULL;
  pcxname = NULL;


  getline(f,buffer);		// read first line, a comment

  if (strncmp(buffer.c_str(), "// ", 3)) {
	printf("NFO file missing header lines and version info\n");
	exit(1);
  }
  getline(f,buffer);		// second line used to get info version
  if (strncmp(buffer.c_str(), "// (Info version ", 17)) {
	infover = 1;
	f.seekg(0);
  } else
	infover = atoi(buffer.c_str() + 17);

  if (infover > 2)
	getline(f,buffer);	// skip "format: " line


  colourmap = NULL;

  try{
	read_file(f,infover,file);
  }catch(Sprite::unparseable e){
	printf(e);
	exit(1);
  }
}

inforeader::~inforeader() 
{
	delete(pcx);
}

void inforeader::PrepareReal(const Real&sprite){
  if ( sprite.reopen() || !pcx || !pcxname || (stricmp(sprite.GetName(), pcxname) != 0) ) {
	// new file

	delete(pcx);

	printf("Loading %s\n", sprite.GetName());

	pcxname = sprite.GetName();
	pcx = new pcxread(new singlefile(pcxname, "rb", NULL));
	if (!pcx) {
		printf("\nError: can't open %s\n", pcxname);
		exit(2);
	}

	if (colourmap)
		pcx->installreadmap(colourmap);

	pcx->startimage(0, 0, 0, 0, NULL);
  }

  inf = sprite.inf;

  sx = (inf[3] << 8) | inf[2];
  sy = inf[1];

  pcx->startsubimage(sprite.x(), sprite.y(), sx, sy);

  imgsize = (long) sx * (long) sy;
}

int inforeader::getsprite(U8 *sprite)
{
  pcx->streamgetpixel(sprite, imgsize);
  return 1;
}

void inforeader::installmap(int *map)
{
	colourmap = map;
}


infowriter::infowriter(FILE *info, int maxboxes, int useplaintext)
{
  infowriter::info = info;
  fputs("// Automatically generated by GRFCODEC. Do not modify!\n", info);
  fputs("// (Info version 6)\n", info);
  fputs("// Format: spritenum pcxfile xpos ypos compression ysize xsize xrel yrel\n", info);

  infowriter::useplaintext = useplaintext;
  infowriter::maxboxes = maxboxes;
  boxes = new box[maxboxes];
  boxnum = 0;
  spriteno = 0;
  for (int i=0; i<maxboxes; i++)
	boxes[i].type = isinvalid;
}

void infowriter::newband(pcxfile *pcx)
{
  int i;
  U16 j;

  for (i=0; i<boxnum; i++) {
	fprintf(info, "%5d ", spriteno++);
	switch (boxes[i].type) {
		case issprite: {
			box::foo::boxsprite *s = &(boxes[i].h.sprite);

			fprintf(info, infoline, pcx->filename(), s->x,
				pcx->subimagey(),
				s->info[0], s->info[1],
				makeint(s->info[2], s->info[3]),
				makeint(s->info[4], s->info[5]),
				makeint(s->info[6], s->info[7]));
			fputs("\n", info);
			break;  }

		case isdata:	{
			box::foo::boxdata *d = &(boxes[i].h.data);
			int instr = 0;
			int count = 0;

			if (d->data[0] == 0xff && d->size>4 && spriteno>1) {	// binary include file
				int namelen = d->data[1];
				char *filename = new char[strlen(pcx->getdirectory())+namelen+1];

				strcpy(filename, pcx->getdirectory());
				strcat(filename, (char*) d->data + 2);
				fprintf(info, "**\t %s\n", filename);

				FILE *bin = fopen(filename, "wb");
				if (!bin) {
					fperror("Cannot write to %s", filename);
					exit(2);
				}
				fwrite(d->data+3+namelen, d->size-3-namelen, 1, bin);
				delete[]filename;
				fclose(bin);
			} else {
			    fprintf(info, "* %d\t", d->size);
			    for (j=0; j<d->size; j++) {
				// Readable characters are 32..126 (excluding " (34)) and 158..255
#define istxt(x) (j+(x)<d->size && ((d->data[j+(x)]>=32 && d->data[j+(x)]!=34 && d->data[j+(x)]<127) || d->data[j+(x)] > 158))
				//int thistxt = (d->data[j] >= 32 && d->data[j] < 127) || d->data[j] > 158;
				//int nexttxt = j+1<d->size && ((d->data[j+1]>=32 && d->data[j+1]<127) || d->data[j+1] > 158);

				if (istxt(0) && useplaintext && (instr || 
						//two in a row for 8 and E
						(istxt(1) && (d->data[0]==8 || d->data[0]==0xE ||
						//four in a row for everything else.
						(istxt(2) && istxt(3))
						))
					)) {
					if (!instr) {
						fputs(" \"", info); instr = 1;
					}
					fputc(d->data[j], info);
				} else {
					if (instr) {
						fputs("\"", info); instr = 0;
					}
					fprintf(info, " %02X", d->data[j]);
				}

				// break lines after 32 non-text characters
				// or at spaces after 32 text characters or
				// after 60 text characters
				count++;
				if ( ((!instr && count>=32) ||
				      (instr && (count>=32 && d->data[j]==' ' 
						|| count>=50)))
				     && j<d->size-1) {
					if (instr) {
						if(istxt(1)){
							fputs("\"\n\t \"", info);
						}else{
							fputs("\"\n\t", info); instr = 0;
						}
					}else
						fputs("\n\t", info);
					count = 0;
				}
			    }

			    if (instr) fputs("\"", info);
			    fputs("\n", info);
			}
			delete(d->data);
			break;	}

		default:
			printf("\nHuh? What kind of box is that?\n");
			exit(2);
	}
  }

  boxnum = 0;

  for (i=0; i<maxboxes; i++)
	boxes[i].type = isinvalid;
}

void infowriter::resize(int newmaxboxes)
{
  printf("Reallocating memory for %d boxes\n", newmaxboxes);
  infowriter::maxboxes = newmaxboxes;
  box *newboxes = new box[newmaxboxes];
  int i;
  for (i=0; i<maxboxes; i++)
	newboxes[i] = boxes[i];
  for (; i<newmaxboxes; i++)
	newboxes[i].type = isinvalid;
  delete(boxes);
  boxes = newboxes;
}

void infowriter::addsprite(int x, U8 info[8])
{
  if (boxnum >= maxboxes)
	resize(maxboxes*2);

  boxes[boxnum].type = issprite;
  boxes[boxnum].h.sprite.x = x;
  memcpy(boxes[boxnum].h.sprite.info, info, 8*sizeof(U8));

  boxnum++;
}

void infowriter::adddata(U16 size, U8 *data)
{
  if (boxnum >= maxboxes)
	resize(maxboxes*2);

  boxes[boxnum].type = isdata;
  boxes[boxnum].h.data.size = size;
  boxes[boxnum].h.data.data = data;

  boxnum++;
}

void infowriter::done(int count)
{
  if (count != spriteno) {
	printf("\nError: GRFCodec thinks it has written %d sprites but the info file only got %d\n", count, spriteno);
	exit(2);
  }
}


