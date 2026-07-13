#define INITGUID
#include <windows.h>
#include <stdio.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dinputd.h>
DEFINE_GUID(CLSID_GenericFFBDriver,0x0AB5665A,0x4549,0x4FD0,0xA9,0x52,0x5A,0x2B,0x96,0x99,0xBD,0xA8);
#include "effect_driver.h"
#include "hid_rumble.h"
LONG g_serverLocks=0; LONG g_objCount=0; HINSTANCE g_hInst=nullptr;
int main(){
  printf("step1\n"); fflush(stdout);
  FfbEnableDriverDryRun(true);
  printf("step2\n"); fflush(stdout);
  EffectDriver* d = new EffectDriver();
  printf("step3 ref\n"); fflush(stdout);
  IDirectInputEffectDriver* drv=nullptr;
  d->QueryInterface(IID_IDirectInputEffectDriver,(void**)&drv);
  d->Release();
  printf("step4 device\n"); fflush(stdout);
  drv->DeviceID(0x800,0,TRUE,1,nullptr);
  printf("step5 download\n"); fflush(stdout);
  DICONSTANTFORCE cf{5000};
  DWORD axes[2]={0,4};
  LONG dir[2]={0,0};
  DIEFFECT eff{};
  eff.dwSize=sizeof(DIEFFECT);
  eff.dwFlags=DIEFF_CARTESIAN;
  eff.dwDuration=100000;
  eff.dwGain=10000;
  eff.cAxes=2;
  eff.rgdwAxes=axes;
  eff.rglDirection=dir;
  eff.cbTypeSpecificParams=sizeof(cf);
  eff.lpvTypeSpecificParams=&cf;
  DWORD h=0;
  HRESULT hr=drv->DownloadEffect(1,0,&h,&eff,DIEP_ALLPARAMS|DIEP_START);
  printf("download hr=%08lx h=%u\n",(unsigned long)hr,h); fflush(stdout);
  Sleep(30);
  drv->StopEffect(1,h);
  drv->DestroyEffect(1,h);
  drv->DeviceID(0x800,0,FALSE,1,nullptr);
  drv->Release();
  printf("done obj=%ld\n",g_objCount); fflush(stdout);
  return 0;
}
