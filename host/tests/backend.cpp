#include <HostRuntime.h>
#include <HostMemory.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <Preferences.h>
#include <SDCardManager.h>
#include <FreeInkUIDisplayTarget.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(){
 using namespace freeink::host;namespace fs=std::filesystem;
 char path[]="/tmp/freeink-host-XXXXXX";assert(mkdtemp(path));auto root=fs::path(path);
 runtime().sdRoot=(root/"sd").string();runtime().stateRoot=(root/"state").string();runtime().outputRoot=(root/"output").string();
 fs::create_directories(runtime().sdRoot);fs::create_directories(runtime().stateRoot);
 assert(SdMan.begin());assert(SdMan.ensureDirectoryExists("/books"));
 auto file=SdMan.open("/books/test.txt",O_RDWR|O_CREAT|O_TRUNC);assert(file);assert(file.write("abcdef",6)==6);assert(file.fileSize()==6);
 assert(file.seekSet(2));char data[4]{};assert(file.read(data,3)==3);assert(std::string(data)=="cde");assert(file.sync());
 assert(!SdMan.open("/../outside",O_CREAT|O_RDWR));fs::create_directory_symlink(root,runtime().sdRoot+"/escape");assert(!SdMan.open("/escape/secret",O_CREAT|O_RDWR));assert(!fs::exists(root/"secret"));
 assert(SdMan.detachFilesystemForRawAccess());assert(!file);assert(file.read(data,1)==-1);assert(!SdMan.open("/books/test.txt"));assert(SdMan.begin());assert(!file);assert(SdMan.open("/books/test.txt"));
 assert(SdMan.rename("/books/test.txt","/books/moved.txt"));assert(!SdMan.exists("/books/test.txt"));
 Preferences prefs;assert(prefs.begin("test"));assert(prefs.putUChar("brightness",70));assert(prefs.putBool("light",true));Preferences restored;assert(restored.begin("test"));assert(restored.getUChar("brightness")==70);assert(restored.getBool("light"));
 InputManager input;assert(input.beginAsync());input.hostKey(InputManager::BTN_DOWN,true);input.hostKey(InputManager::BTN_DOWN,true);uint8_t key;assert(input.popPress(key)&&key==InputManager::BTN_DOWN);assert(!input.popPress(key));input.hostKey(key,false);assert(!input.isPressed(key));
 input.hostHome(true);bool held;assert(input.popHomeKey(held)&&held);assert(!input.popHomeKey(held));
 // A logical corner drawn by the production target must appear in that same
 // corner in exported pixels for every rotation, independent of SDL scaling.
 EInkDisplay display(-1,-1,-1,-1,-1,-1);display.begin();
 using namespace freeink::ui;
 for(auto orientation:{Orientation::Portrait,Orientation::PortraitInverted,Orientation::LandscapeClockwise,Orientation::LandscapeCounterClockwise}){
  display.clearScreen();DisplayTarget target(display.getFrameBuffer(),display.getDisplayWidth(),display.getDisplayHeight(),display.getDisplayWidthBytes(),orientation);
  runtime().orientation=int(orientation);target.fill({0,0,5,7},Paint::solid(Color::Black));display.displayBuffer();auto f=snapshot();assert(f.pixels[0]==0);assert(f.pixels[6*f.width+4]==0);assert(f.pixels.back()==255);
 }
 fs::remove_all(root);std::cout<<"Host storage, preferences, input and four display rotations passed\n";
}
