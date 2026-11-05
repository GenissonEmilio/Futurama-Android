package com.genisson.futuramarecomp;

import java.util.Arrays;

/** Pointer ownership and layout, independent of Android so multitouch regressions run on the host too. */
final class TouchControlModel {
    static final int A=0, B=1, X=2, Y=3, SPECIAL=4, AIM=5, PREVIOUS=6, NEXT=7,
        VISION=8, PAUSE=9, SETTINGS=10, BACK=11, DODGE=12, COUNT=13;
    static final int MOVE=100, LOOK=101, NONE=-1;
    static final int FRY=1, BENDER=2, LEELA=3, ZOIDBERG=4;
    final float[] cx=new float[COUNT], cy=new float[COUNT], radius=new float[COUNT];
    final boolean[] visible=new boolean[COUNT];
    final int[] masks={0x1000,0x2000,0x4000,0x8000,0x5000,0x400,0x100,0x200,0x80,16,0,32,0x800};
    private final int[] ids=new int[10], roles=new int[10];
    private final float[] px=new float[10], py=new float[10];
    float width, height, moveX, moveY, moveRadius, lx, ly, lookDx, lookDy;
    private float minimumHit, left, top, right, bottom;
    int buttons, character, weapon, context, lookPointer=-1, movePointer=-1;
    boolean gameplay, firstPerson, hasLooked, touchVisible=true;

    TouchControlModel() { Arrays.fill(ids,-1); Arrays.fill(roles,NONE); updateVisibility(); }

    boolean setState(long state) {
        int nextCharacter=(int)(state&7), nextWeapon=(int)((state>>>8)&255);
        boolean nextGameplay=(state&64)!=0;
        boolean remap=nextCharacter!=character || nextGameplay!=gameplay;
        boolean changed=remap || nextWeapon!=weapon || context!=((state>>>3)&7) || firstPerson!=((state&65536)!=0);
        if(remap) clear();
        character=nextCharacter; weapon=nextWeapon; gameplay=nextGameplay;
        context=(int)((state>>>3)&7); firstPerson=(state&65536)!=0;
        if(changed) { updateVisibility(); recompute(); }
        return changed;
    }

    void layout(float w,float h,float density,float insetLeft,float insetTop,float insetRight,float insetBottom) {
        clear(); width=w; height=h;
        left=insetLeft; top=insetTop; right=w-insetRight; bottom=h-insetBottom;
        float aw=Math.max(1,right-left), ah=Math.max(1,bottom-top);
        minimumHit=24*Math.max(1,density);
        place(A,.912f,.80f,.078f,aw,ah); place(B,.814f,.91f,.054f,aw,ah);
        place(X,.795f,.71f,.060f,aw,ah); place(Y,.90f,.59f,.061f,aw,ah);
        place(SPECIAL,.795f,.51f,.053f,aw,ah); place(AIM,.95f,.405f,.044f,aw,ah);
        place(PREVIOUS,.89f,.29f,.037f,aw,ah); place(NEXT,.96f,.29f,.037f,aw,ah);
        place(VISION,.96f,.17f,.037f,aw,ah); place(PAUSE,.96f,.065f,.033f,aw,ah);
        place(SETTINGS,.90f,.065f,.033f,aw,ah); place(BACK,.245f,.91f,.035f,aw,ah);
        place(DODGE,.95f,.405f,.044f,aw,ah);
        moveRadius=Math.min(aw*.105f,ah*.135f);
        moveX=Math.max(left+moveRadius+ah*.025f,left+aw*.12f);
        moveY=Math.min(bottom-moveRadius-ah*.025f,top+ah*.78f);
        if(character==ZOIDBERG && gameplay) {
            cx[B]=left+aw*.90f; cy[B]=top+ah*.81f; radius[B]=ah*.078f;
        }
    }

    private void place(int button,float x,float y,float r,float aw,float ah) {
        radius[button]=Math.min(ah*r,aw*.07f);
        cx[button]=Math.max(left+radius[button],Math.min(right-radius[button],left+x*aw));
        cy[button]=Math.max(top+radius[button],Math.min(bottom-radius[button],top+y*ah));
    }

    private void updateVisibility() {
        Arrays.fill(visible,false);
        visible[PAUSE]=visible[SETTINGS]=true;
        if(!touchVisible)return;
        if(!gameplay) { visible[A]=visible[B]=visible[BACK]=true; return; }
        visible[B]=visible[Y]=true;
        if(character==ZOIDBERG) return;
        visible[A]=visible[X]=visible[VISION]=true;
        if(character==FRY) {
            visible[PREVIOUS]=visible[NEXT]=true;
            visible[AIM]=weapon>=2;
        } else if(character==BENDER || character==LEELA) {
            visible[SPECIAL]=true;
            visible[AIM]=true; // LT recenters the original follow camera.
            visible[DODGE]=character==LEELA;
        }
        // Leela's dodge occupies the aim position; recenter remains available from the options panel.
        if(visible[DODGE])visible[AIM]=false;
    }

    void setTouchVisible(boolean show) { clear(); touchVisible=show; updateVisibility(); }
    boolean cameraEnabled() { return touchVisible && gameplay && character!=ZOIDBERG; }
    boolean isPressed(int button) { return (buttons&masks[button])==masks[button] && masks[button]!=0; }
    float hitRadius(int button) { return Math.max(radius[button]*1.2f,minimumHit); }

    int hit(float x,float y) {
        int nearest=NONE; float best=Float.MAX_VALUE;
        for(int b=0;b<COUNT;++b) if(visible[b]) {
            float r=hitRadius(b), dx=x-cx[b],dy=y-cy[b],d=(dx*dx+dy*dy)/(r*r);
            if(d<=1 && d<best) { best=d; nearest=b; }
        }
        if(nearest!=NONE)return nearest;
        if(touchVisible && moveRadius>0 && Math.hypot(x-moveX,y-moveY)<=moveRadius*1.4f)return MOVE;
        if(cameraEnabled() && x>left+(right-left)*.36f && x<right && y>top+(bottom-top)*.13f && y<bottom)
            return LOOK;
        return NONE;
    }

    int down(int id,float x,float y) {
        int existing=slot(id); if(existing>=0)return roles[existing];
        int role=hit(x,y);
        if((role==MOVE&&movePointer!=-1)||(role==LOOK&&lookPointer!=-1))role=NONE;
        for(int i=0;i<ids.length;++i) if(ids[i]==-1) {
            ids[i]=id; roles[i]=role; px[i]=x; py[i]=y;
            if(role==MOVE)movePointer=id; if(role==LOOK)lookPointer=id;
            recompute(); return role;
        }
        return NONE;
    }

    void move(int id,float x,float y) {
        if(!Float.isFinite(x)||!Float.isFinite(y))return;
        int s=slot(id); if(s<0)return;
        if(roles[s]==LOOK) {
            lookDx+=x-px[s]; lookDy+=y-py[s];
            if(Math.abs(x-px[s])+Math.abs(y-py[s])>1)hasLooked=true;
        }
        px[s]=x; py[s]=y; recompute();
    }

    int up(int id,float x,float y) {
        int s=slot(id); if(s<0)return NONE;
        int role=roles[s];
        if(role==LOOK) { move(id,x,y); lookPointer=-1; }
        if(role==MOVE)movePointer=-1;
        boolean click=role>=0&&role<COUNT && Math.hypot(x-cx[role],y-cy[role])<=hitRadius(role)*1.4f;
        ids[s]=-1; roles[s]=NONE; recompute();
        return click?role:NONE;
    }

    void clear() {
        Arrays.fill(ids,-1); Arrays.fill(roles,NONE); movePointer=lookPointer=-1;
        buttons=0; lx=ly=lookDx=lookDy=0;
    }
    private int slot(int id) { for(int i=0;i<ids.length;++i)if(ids[i]==id)return i; return -1; }
    private void recompute() {
        buttons=0; lx=ly=0;
        for(int s=0;s<ids.length;++s) if(ids[s]!=-1) {
            int role=roles[s];
            if(role==MOVE && moveRadius>0) {
                float x=(px[s]-moveX)/moveRadius, y=(moveY-py[s])/moveRadius;
                float length=(float)Math.max(1,Math.hypot(x,y)); lx=x/length; ly=y/length;
            } else if(role>=0&&role<COUNT && visible[role] &&
                      Math.hypot(px[s]-cx[role],py[s]-cy[role])<=hitRadius(role)*1.4f)buttons|=masks[role];
        }
        if(!gameplay)buttons|=(lx<-.45f?4:lx>.45f?8:0)|(ly>.45f?1:ly<-.45f?2:0);
    }
}
