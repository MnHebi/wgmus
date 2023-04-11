int WasapiVolumeConfig(float wasapiVol, DWORD streamVol);
void wgmus_config();
int bass_init();
int bass_pause();
int bass_stop();
int bass_resume();
int bass_clear();
int bass_forceplay(const char *path);
int bass_play(const char *path);