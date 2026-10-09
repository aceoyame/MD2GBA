#include "md2gba_cycles.h"
#include "md2gba_interp_opclass.h"
static uint16_t rw(const Md2GbaBus*b,uint32_t a){if(!b||!b->rom||a+1u>=b->rom_size)return 0;return (uint16_t)(((uint16_t)b->rom[a]<<8)|b->rom[a+1u]);}
uint16_t md68k_estimate_cycles(const Md2GbaBus*b,uint32_t pc,uint32_t next){uint16_t op=rw(b,pc&0xFFFFFFu);uint8_t cl=md68k_interp_opclass[op];unsigned mode=(op>>3)&7u;switch(cl){
case MD68KI_NOP:case MD68KI_MOVEQ:case MD68KI_EXT:case MD68KI_SWAP:case MD68KI_EXG:return 4;
case MD68KI_BRA:case MD68KI_BSR:case MD68KI_BCC:{uint32_t fall=(pc+((op&0xFFu)?2u:4u))&0xFFFFFFu;return (cl==MD68KI_BCC&&next==fall)?8:10;}
case MD68KI_DBCC:return 10;
case MD68KI_RTS:return 16;case MD68KI_RTE:return 20;case MD68KI_TRAP:return 34;case MD68KI_JSR:return 18;case MD68KI_JMP:return 10;
case MD68KI_MULU:case MD68KI_MULS:return 54;case MD68KI_DIVU:case MD68KI_DIVS:return 140;
case MD68KI_MOVEM_FROM_REGS:case MD68KI_MOVEM_TO_REGS:return 16;
case MD68KI_MOVE:case MD68KI_MOVEA:return mode<=1u?4:8;
case MD68KI_ADDQ:case MD68KI_SUBQ:return mode<=1u?4:8;
case MD68KI_SHIFT_REG:return 8;
case MD68KI_ASR_MEM:case MD68KI_ASL_MEM:case MD68KI_LSR_MEM:case MD68KI_LSL_MEM:case MD68KI_ROXR_MEM:case MD68KI_ROXL_MEM:case MD68KI_ROR_MEM:case MD68KI_ROL_MEM:return 12;
default:return mode<=1u?6:10;}}
