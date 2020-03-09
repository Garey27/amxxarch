#pragma once


bool OnMetaAttach();
void OnMetaDetach();
void pfnUpdateClientData(const struct edict_s *ent, int sendweapons, struct clientdata_s *cd);
void pfnPlaybackEvent(int flags, const edict_t *pInvoker, unsigned short eventindex, float delay, float *origin, float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2);
void C_ServerActivate(edict_t *pEdictList, int edictCount, int clientMax);
void pfnCmdStart(const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed);
