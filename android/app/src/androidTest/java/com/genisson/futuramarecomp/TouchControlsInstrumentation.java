package com.genisson.futuramarecomp;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;

/** Exercises Android pointer indexing and the View-to-native sink boundary without game data. */
public final class TouchControlsInstrumentation extends Instrumentation {
    private int checks;
    private long eventTime;
    private final long downTime = SystemClock.uptimeMillis();

    private static final class FakeSink implements TouchControlsView.Sink {
        int buttons, lookCalls, clears, settingsCalls;
        float lx, ly, dx, dy;
        long packedState;
        public boolean running() { return true; }
        public long state() { return packedState; }
        public void touch(int buttons, float lx, float ly) { this.buttons=buttons; this.lx=lx; this.ly=ly; }
        public void look(float dx, float dy) { this.dx=dx; this.dy=dy; ++lookCalls; }
        public void clearTouch() { buttons=0; lx=ly=dx=dy=0; ++clears; }
        public void settings() { ++settingsCalls; }
    }

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Throwable[] mainFailure=new Throwable[1];
            runOnMainSync(() -> {
                try { runTests(); } catch (Throwable failure) { mainFailure[0]=failure; }
            });
            if (mainFailure[0]!=null) throw mainFailure[0];
            result.putString("summary", "Touch controls instrumentation passed ("+checks+" checks)");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("summary", "Touch controls instrumentation failed");
            result.putString("stack", Log.getStackTraceString(failure));
            finish(Activity.RESULT_CANCELED, result);
        }
    }
    private void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }
    private void near(float actual, float expected, String message) {
        check(Math.abs(actual-expected)<.0001f, message+": "+actual+" != "+expected);
    }
    private void event(TouchControlsView view, int action, int changed, int[] ids, float... xy) {
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] coordinates=new MotionEvent.PointerCoords[ids.length];
        for (int i=0;i<ids.length;++i) {
            properties[i]=new MotionEvent.PointerProperties();
            properties[i].id=ids[i]; properties[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coordinates[i]=new MotionEvent.PointerCoords();
            coordinates[i].x=xy[i*2]; coordinates[i].y=xy[i*2+1];
            coordinates[i].pressure=coordinates[i].size=1;
        }
        int encoded=action | (changed << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
        MotionEvent motion=MotionEvent.obtain(downTime, Math.max(downTime, SystemClock.uptimeMillis())+(++eventTime),
                encoded, ids.length, properties, coordinates, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        try { check(view.onTouchEvent(motion), "touch event is consumed"); }
        finally { motion.recycle(); }
    }
    private void tap(TouchControlsView view, int button) {
        float x=view.model.cx[button], y=view.model.cy[button];
        event(view, MotionEvent.ACTION_DOWN, 0, new int[]{3}, x,y);
        event(view, MotionEvent.ACTION_UP, 0, new int[]{3}, x,y);
    }
    private void profile(TouchControlsView view, FakeSink sink, int character) {
        sink.packedState=character | 64 | (2L<<8);
        view.clear(); view.model.setState(sink.packedState);
        view.model.layout(1920,1080,1,0,0,0,0);
    }
    private void runTests() {
        // Keep camera preference writes isolated from the player's settings.
        Context isolated=new ContextWrapper(getTargetContext()) {
            @Override public SharedPreferences getSharedPreferences(String name, int mode) {
                return super.getSharedPreferences("instrumentation-"+name, mode);
            }
        };
        try {
            FakeSink sink=new FakeSink();
            TouchControlsView view=new TouchControlsView(isolated,sink);
            view.layout(0,0,1920,1080);
            profile(view,sink,TouchControlModel.FRY);
            TouchControlModel m=view.model;
            float mx=m.moveX, my=m.moveY, r=m.moveRadius, ax=m.cx[TouchControlModel.A], ay=m.cy[TouchControlModel.A];
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{7},mx,my);
            event(view,MotionEvent.ACTION_POINTER_DOWN,1,new int[]{7,22},mx,my,1100,420);
            event(view,MotionEvent.ACTION_POINTER_DOWN,2,new int[]{7,22,3},mx,my,1100,420,ax,ay);
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{7,22,3},mx+.6f*r,my-.8f*r,1120,408,ax,ay);
            near(sink.lx,.6f,"three fingers retain movement X"); near(sink.ly,.8f,"three fingers retain movement Y");
            check(sink.buttons==0x1000,"third pointer sends original A mask");
            near(sink.dx,20f/1080,"camera right drag reaches sink in normalized raw units");
            near(sink.dy,-12f/1080,"camera upward drag preserves sign");
            int calls=sink.lookCalls;
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{3,22,7},ax,ay,1120,408,mx-r,my);
            check(sink.lookCalls==calls,"reordered stationary look pointer produces no drift");
            near(sink.lx,-1,"movement follows ID after pointer indices reorder");
            event(view,MotionEvent.ACTION_POINTER_UP,0,new int[]{3,22,7},ax,ay,1120,408,mx-r,my);
            check(sink.buttons==0 && m.lookPointer==22 && m.movePointer==7,"action lift preserves move and camera");
            event(view,MotionEvent.ACTION_POINTER_UP,1,new int[]{22,7},1120,408,mx-r,my);
            near(sink.lx,0,"movement pointer up clears X"); near(sink.ly,0,"movement pointer up clears Y");
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{22},1130,418);
            near(sink.dx,10f/1080,"camera continues after the other two fingers lift");
            event(view,MotionEvent.ACTION_UP,0,new int[]{22},1130,418);
            check(m.lookPointer==-1,"final lift drops camera owner");
            calls=sink.lookCalls;
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{3},ax,ay);
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{3},ax,ay);
            check(sink.lookCalls==calls,"action-only gesture adds no residual camera input");
            int clears=sink.clears;
            event(view,MotionEvent.ACTION_CANCEL,0,new int[]{3},ax,ay);
            check(sink.clears==clears+1 && sink.buttons==0 && m.movePointer==-1 && m.lookPointer==-1,
                    "Android cancel calls clearTouch and releases model owners");

            view.setCameraOptions(1.5f,true,true);
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{22},1100,420);
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{22},1120,430);
            near(sink.dx,-30f/1080,"horizontal inversion and sensitivity reach sink");
            near(sink.dy,-15f/1080,"vertical inversion and sensitivity reach sink");
            event(view,MotionEvent.ACTION_UP,0,new int[]{22},1120,430);
            view.setCameraOptions(1,false,false);
            m.setState(sink.packedState | 65536);
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{22},1100,420);
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{22},1120,430);
            near(sink.dx,-20f/1080,"first person compensates original horizontal camera direction");
            near(sink.dy,10f/1080,"first person preserves vertical camera direction");
            event(view,MotionEvent.ACTION_UP,0,new int[]{22},1120,430);

            profile(view,sink,TouchControlModel.FRY);
            mx=m.moveX; my=m.moveY; r=m.moveRadius;
            float aimX=m.cx[TouchControlModel.AIM], aimY=m.cy[TouchControlModel.AIM];
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{7},mx,my);
            event(view,MotionEvent.ACTION_POINTER_DOWN,1,new int[]{7,3},mx+.6f*r,my,aimX,aimY);
            check(sink.buttons==0x400 && m.movePointer==7,"armed Fry holds aim alongside movement");
            clears=sink.clears;
            sink.packedState=TouchControlModel.FRY | 64 | (1L<<8);
            view.refreshState();
            check(sink.buttons==0 && !m.visible[TouchControlModel.AIM],"weapon poll immediately releases hidden held LT without a touch event");
            near(sink.lx,.6f,"weapon poll preserves held movement");
            check(m.movePointer==7 && sink.clears==clears,"weapon update keeps pointer ownership without global clear");
            sink.packedState |= 2L<<3;
            view.refreshState();
            check(m.context==2 && view.getContentDescription().toString().contains("PORTA"),"native door context updates accessible Y action");
            near(sink.lx,.6f,"context poll preserves movement axis");
            check(m.movePointer==7 && sink.clears==clears,"context poll retains held movement ownership");
            ax=m.cx[TouchControlModel.A]; ay=m.cy[TouchControlModel.A];
            event(view,MotionEvent.ACTION_POINTER_UP,1,new int[]{7,3},mx+.6f*r,my,aimX,aimY);
            event(view,MotionEvent.ACTION_POINTER_DOWN,1,new int[]{7,3},mx+.6f*r,my,ax,ay);
            check(sink.buttons==0x1000,"action is held before polled character transition");
            sink.packedState=TouchControlModel.BENDER | 64;
            view.refreshState();
            check(sink.clears==clears+1 && sink.buttons==0 && sink.lx==0 && sink.ly==0,
                    "character poll immediately clears sink action and axes");
            check(m.movePointer==-1 && m.lookPointer==-1,"character poll drops old gesture owners");
            check(view.getContentDescription().toString().contains("Bender"),"character poll updates accessible profile description");
            event(view,MotionEvent.ACTION_CANCEL,0,new int[]{7,3},mx+.6f*r,my,ax,ay);

            for (int character : new int[]{TouchControlModel.BENDER,TouchControlModel.LEELA}) {
                profile(view,sink,character);
                float x=m.cx[TouchControlModel.SPECIAL], y=m.cy[TouchControlModel.SPECIAL];
                event(view,MotionEvent.ACTION_DOWN,0,new int[]{3},x,y);
                check(sink.buttons==0x5000,"melee special sends original A|X for character "+character);
                event(view,MotionEvent.ACTION_UP,0,new int[]{3},x,y);
                check(sink.buttons==0,"melee special releases both bits");
            }
            profile(view,sink,TouchControlModel.ZOIDBERG);
            check(m.visible[TouchControlModel.B] && m.visible[TouchControlModel.Y] && !m.visible[TouchControlModel.A]
                    && !m.visible[TouchControlModel.X] && !m.visible[TouchControlModel.SPECIAL],"Zoidberg has sparse minigame actions");
            calls=sink.lookCalls;
            event(view,MotionEvent.ACTION_DOWN,0,new int[]{22},1100,420);
            event(view,MotionEvent.ACTION_MOVE,0,new int[]{22},1200,500);
            event(view,MotionEvent.ACTION_UP,0,new int[]{22},1200,500);
            check(sink.lookCalls==calls,"Zoidberg never sends camera drag");
            int settings=sink.settingsCalls;
            tap(view,TouchControlModel.SETTINGS);
            check(sink.settingsCalls==settings+1 && sink.buttons==0,"settings release invokes callback and clears held input");
            view.setTouchVisible(false);
            tap(view,TouchControlModel.SETTINGS);
            check(sink.settingsCalls==settings+2,"settings stays usable with hidden touch HUD");
            view.clear();
        } finally {
            isolated.getSharedPreferences("touch-controls",Context.MODE_PRIVATE).edit().clear().commit();
        }
    }
}
