#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>
#include <cstdio>
#include <string>
#include "engine/core/log.h"
static DWORD main_thread=0; static HRESULT com_result=S_OK; static int initialized=0, uninitialized=0, refs=0, early_uninit=0;
static bool create_error=false, activate_error=false, format_error=false;
#define UNKNOWN_METHODS HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;} ULONG STDMETHODCALLTYPE AddRef() override{return 1;} ULONG STDMETHODCALLTYPE Release() override{--refs;return 1;}
#define STUB(method, args) HRESULT STDMETHODCALLTYPE method args override {return E_NOTIMPL;}
static AudioObjectType offered=AudioObjectType_None, failure=AudioObjectType_None;
static bool mask_error=false; static int starts=0;
static int default_calls=0, get_device_calls=0; static bool get_device_error=false; static UINT32 dynamic_objects=0;
static std::wstring current_id, requested_id;
static LPWSTR CopyWide(const std::wstring& text){auto* out=static_cast<LPWSTR>(CoTaskMemAlloc((text.size()+1)*sizeof(wchar_t)));if(out)wcscpy_s(out,text.size()+1,text.c_str());return out;}
struct Object:ISpatialAudioObject {
 UNKNOWN_METHODS
 STUB(GetBuffer,(BYTE**,UINT32*)) STUB(SetEndOfStream,(UINT32)) STUB(IsActive,(BOOL*)) STUB(GetAudioObjectType,(AudioObjectType*)) STUB(SetPosition,(float,float,float)) STUB(SetVolume,(float))
} object;
struct Stream:ISpatialAudioObjectRenderStream {
 UNKNOWN_METHODS
 STUB(GetAvailableDynamicObjectCount,(UINT32*)) STUB(GetService,(REFIID,void**))
 HRESULT STDMETHODCALLTYPE Start() override{++starts;return S_OK;} HRESULT STDMETHODCALLTYPE Stop() override{return S_OK;}
 STUB(Reset,()) STUB(BeginUpdatingAudioObjects,(UINT32*,UINT32*)) STUB(EndUpdatingAudioObjects,())
 HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObject(AudioObjectType type,ISpatialAudioObject** out) override{if(type==failure)return E_FAIL;*out=&object;++refs;return S_OK;}
} stream;
struct Client:ISpatialAudioClient {
 UNKNOWN_METHODS
 STUB(GetStaticObjectPosition,(AudioObjectType,float*,float*,float*))
 HRESULT STDMETHODCALLTYPE GetNativeStaticObjectTypeMask(AudioObjectType* mask) override{*mask=offered;return mask_error?E_FAIL:S_OK;}
 HRESULT STDMETHODCALLTYPE GetMaxDynamicObjectCount(UINT32* count) override{*count=dynamic_objects;return S_OK;}
 STUB(GetSupportedAudioObjectFormatEnumerator,(IAudioFormatEnumerator**)) STUB(GetMaxFrameCount,(const WAVEFORMATEX*,UINT32*))
 HRESULT STDMETHODCALLTYPE IsAudioObjectFormatSupported(const WAVEFORMATEX*) override{return format_error?E_FAIL:S_OK;}
 STUB(IsSpatialAudioStreamAvailable,(REFIID,const PROPVARIANT*))
 HRESULT STDMETHODCALLTYPE ActivateSpatialAudioStream(const PROPVARIANT*,REFIID,void** out) override{*out=&stream;++refs;return S_OK;}
} client;
struct Properties:IPropertyStore {
 UNKNOWN_METHODS
 STUB(GetCount,(DWORD*)) STUB(GetAt,(DWORD,PROPERTYKEY*)) STUB(SetValue,(REFPROPERTYKEY,REFPROPVARIANT)) STUB(Commit,())
 HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY,PROPVARIANT* value) override{value->vt=VT_LPWSTR;value->pwszVal=CopyWide(current_id==L"{default}"?L"Speakers (Test default)":L"Headphones (Test chosen)");return S_OK;}
} properties;
struct Device:IMMDevice {
 UNKNOWN_METHODS
 HRESULT STDMETHODCALLTYPE Activate(REFIID,DWORD,PROPVARIANT*,void** out) override{if(activate_error)return E_FAIL;*out=&client;++refs;return S_OK;}
 HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD,IPropertyStore** out) override{*out=&properties;++refs;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override{*out=CopyWide(current_id);return S_OK;}
 STUB(GetState,(DWORD*))
} device;
struct Enumerator:IMMDeviceEnumerator {
 UNKNOWN_METHODS
 STUB(EnumAudioEndpoints,(EDataFlow,DWORD,IMMDeviceCollection**))
 HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow,ERole,IMMDevice** out) override{++default_calls;current_id=L"{default}";*out=&device;++refs;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR id,IMMDevice** out) override{++get_device_calls;requested_id=id;if(get_device_error)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);current_id=id;*out=&device;++refs;return S_OK;}
 STUB(RegisterEndpointNotificationCallback,(IMMNotificationClient*)) STUB(UnregisterEndpointNotificationCallback,(IMMNotificationClient*))
} enumerator;
static HRESULT TestCoCreateInstance(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID* out){if(create_error)return E_FAIL;*out=&enumerator;++refs;return S_OK;}
static HRESULT TestCoInitializeEx(LPVOID,DWORD){if(GetCurrentThreadId()!=main_thread)return S_OK;if(SUCCEEDED(com_result))++initialized;return com_result;}
static void TestCoUninitialize(){if(GetCurrentThreadId()==main_thread){++uninitialized;early_uninit+=refs!=0;}}
#define CoInitializeEx TestCoInitializeEx
#define CoUninitialize TestCoUninitialize
#define CoCreateInstance TestCoCreateInstance
#include "engine/audio/spatial_output.cpp"
#undef CoCreateInstance
#undef CoInitializeEx
#undef CoUninitialize
namespace pt {void LogWrite(LogLevel,std::string_view text){std::printf("%.*s\n",static_cast<int>(text.size()),text.data());}}
int main(){
 main_thread=GetCurrentThreadId(); int failed=0; AudioObjectType full=AudioObjectType_None;
 for(auto channel:pt::audio::kBed) full=static_cast<AudioObjectType>(full|channel);
 auto check=[&](bool expected,const char* label){starts=initialized=uninitialized=early_uninit=0;pt::audio::SpatialOutput output;bool opened=output.Open(48000,[](float*,uint32_t,uint32_t){});output.Close();bool ok=opened==expected && (expected?starts==1:starts==0) && initialized==uninitialized && refs==0 && early_uninit==0;std::printf("%s %s\n",ok?"PASS":"FAIL",label);failed+=!ok;};
 offered=full;check(true,"complete bed starts");
 for(auto channel:pt::audio::kBed){offered=static_cast<AudioObjectType>(full&~channel);check(false,"missing static position uses ordinary fallback");}
 offered=full;mask_error=true;check(false,"failed mask query uses ordinary fallback");mask_error=false;
 for(auto channel:pt::audio::kBed){failure=channel;check(false,"failed static object activation uses ordinary fallback");}
 failure=AudioObjectType_None;offered=full;
 com_result=S_FALSE;check(true,"existing MTA initialization is balanced");
 com_result=RPC_E_CHANGED_MODE;check(true,"existing different apartment is not uninitialized");
 com_result=E_OUTOFMEMORY;check(false,"COM initialization failure rejects spatial playback");
 com_result=S_OK;create_error=true;check(false,"failed endpoint creation balances COM");create_error=false;
 activate_error=true;check(false,"failed client activation balances COM");activate_error=false;
 format_error=true;check(false,"unsupported object format balances COM");format_error=false;
 starts=initialized=uninitialized=early_uninit=0;
 {pt::audio::SpatialOutput output;output.Open(48000,[](float*,uint32_t,uint32_t){});output.Open(48000,[](float*,uint32_t,uint32_t){});output.Close();}
 bool reopened=initialized==2 && uninitialized==2 && refs==0 && early_uninit==0;
 std::printf("%s repeated Open balances both initializations\n",reopened?"PASS":"FAIL");failed+=!reopened;
 auto endpoint=[&](const std::string& id,bool expected,bool chosen,const char* name,const char* label){
  default_calls=get_device_calls=starts=initialized=uninitialized=early_uninit=0;requested_id.clear();
  pt::audio::SpatialOutput output;bool opened=output.Open(48000,[](float*,uint32_t,uint32_t){},id);
  bool named=!expected || (output.DeviceName()==name && output.DeviceId()==(chosen?id:std::string("{default}")));
  output.Close();
  bool path=chosen ? (get_device_calls==1 && default_calls==0 && requested_id==std::wstring(id.begin(),id.end())) : (default_calls==1 && get_device_calls==0);
  bool ok=opened==expected && named && path && initialized==uninitialized && refs==0 && early_uninit==0 && output.DeviceName().empty();
  std::printf("%s %s\n",ok?"PASS":"FAIL",label);failed+=!ok;};
 endpoint("",true,false,"Speakers (Test default)","no chosen id opens the default endpoint and reports its name and id");
 endpoint("{0.0.0.00000000}.{chosen}",true,true,"Headphones (Test chosen)","a chosen endpoint id opens that endpoint, never the default");
 dynamic_objects=16;endpoint("{0.0.0.00000000}.{chosen}",true,true,"Headphones (Test chosen)","a spatial format's dynamic objects are logged");dynamic_objects=0;
 get_device_error=true;endpoint("{0.0.0.00000000}.{gone}",false,true,"","a chosen endpoint that is gone fails cleanly (the ordinary output takes over)");get_device_error=false;
 return failed?1:0;
}
