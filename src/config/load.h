/***********************************************************************************************************************************
Configuration Load
***********************************************************************************************************************************/
#ifndef CONFIG_LOAD_H
#define CONFIG_LOAD_H

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void cfgLoad(unsigned int argListSize, const char *argList[]);
void cfgLoadParam(unsigned int argListSize, const char *argList[], String *exe);

#endif
