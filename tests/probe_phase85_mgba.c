#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static color_t pixels[240*160];
static void quiet(struct mLogger*l,int cat,enum mLogLevel level,const char*format,va_list a){(void)l;(void)cat;(void)level;(void)format;(void)a;}
int main(int argc,char**argv){
 if(argc<4)return 2; unsigned frames=atoi(argv[2]); const char*prefix=argv[3];
 struct mLogger logger={.log=quiet};mLogSetDefaultLogger(&logger);
 struct mCore*c=GBACoreCreate();c->init(c);mCoreInitConfig(c,NULL);c->setVideoBuffer(c,pixels,240);c->setAudioBufferSize(c,4096);
 if(!mCoreLoadFile(c,argv[1]))return 3;c->reset(c);
 for(unsigned f=1;f<=frames;f++){
  /* Reproducible title -> opening input, with broad taps for a slow guest. */
  unsigned keys=(f>=420&&f<480)?8:((f>=660&&f<720)?1:0);
  c->setKeys(c,keys);c->runFrame(c);
  if(f%300==0||f==frames){char path[512];FILE*out;
   snprintf(path,sizeof(path),"%s-%05u.ppm",prefix,f);out=fopen(path,"wb");fprintf(out,"P6\n240 160\n255\n");
   for(unsigned i=0;i<240*160;i++){unsigned col=pixels[i];fputc(col&255,out);fputc((col>>8)&255,out);fputc((col>>16)&255,out);}fclose(out);
   snprintf(path,sizeof(path),"%s-%05u.ram",prefix,f);out=fopen(path,"wb");
   for(unsigned a=0x02000000;a<0x02040000;a++)fputc(c->busRead8(c,a),out);
   for(unsigned a=0x03000000;a<0x03008000;a++)fputc(c->busRead8(c,a),out);fclose(out);
   printf("frame=%u display=%04x bg=%03x,%03x,%03x,%03x\n",f,c->busRead16(c,0x04000000),c->busRead16(c,0x04000010),c->busRead16(c,0x04000012),c->busRead16(c,0x04000014),c->busRead16(c,0x04000016));fflush(stdout);
  }
 }
 c->deinit(c);return 0;
}
