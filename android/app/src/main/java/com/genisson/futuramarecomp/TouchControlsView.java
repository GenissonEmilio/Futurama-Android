package com.genisson.futuramarecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

/** Lightweight Canvas controls. The game draws its own health, inventory and ammunition HUD. */
final class TouchControlsView extends View {
    interface Sink {
        boolean running();
        long state();
        void touch(int buttons,float lx,float ly);
        void look(float dx,float dy);
        void clearTouch();
        void settings();
    }
    final TouchControlModel model=new TouchControlModel();
    private final Sink sink;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path=new Path();
    private final float density;
    private float insetLeft,insetTop,insetRight,insetBottom;
    private float sensitivity=1;
    private boolean invertX,invertY;
    private static final int CREAM=0xffecf3ec, CYAN=0xff72c7dc, GOLD=0xffffd35d;
    private static final int[] BADGE={0xff8fdd52,0xffef6b58,0xff51b9ed,GOLD};
    private static final String[] LETTER={"A","B","X","Y","A+X"};
    private final Runnable poll=new Runnable() {
        @Override public void run() {
            if(!isAttachedToWindow()||getVisibility()!=VISIBLE)return;
            refreshState();
            postDelayed(this,100);
        }
    };
    void refreshState() {
        if(!sink.running())return;
        int previousCharacter=model.character; boolean previousGameplay=model.gameplay;
        if(model.setState(sink.state())) {
            if(previousCharacter!=model.character||previousGameplay!=model.gameplay) {
                layoutModel(); sink.clearTouch();
            }
            // A weapon change can hide a held aim button; publish that release immediately too.
            sendTouch(); setContentDescription(description()); invalidate();
        }
    }

    TouchControlsView(Context context,Sink sink) {
        super(context); this.sink=sink; density=getResources().getDisplayMetrics().density;
        paint.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        paint.setStrokeCap(Paint.Cap.ROUND); paint.setStrokeJoin(Paint.Join.ROUND);
        setContentDescription(description());
        setOnApplyWindowInsetsListener((v,insets)->{
            float l=0,t=0,r=0,b=0;
            if(Build.VERSION.SDK_INT>=28) {
                DisplayCutout cutout=insets.getDisplayCutout();
                if(cutout!=null) { l=cutout.getSafeInsetLeft(); t=cutout.getSafeInsetTop(); r=cutout.getSafeInsetRight(); b=cutout.getSafeInsetBottom(); }
            }
            if(l!=insetLeft||t!=insetTop||r!=insetRight||b!=insetBottom) {
                insetLeft=l; insetTop=t; insetRight=r; insetBottom=b;
                clear(); layoutModel(); invalidate();
            }
            return insets;
        });
    }
    private String description() {
        String name=model.character==1?"Fry":model.character==2?"Bender":model.character==3?"Leela":model.character==4?"Zoidberg":"menus";
        return "Controles de "+name+". "+(model.gameplay?(model.firstPerson?"Primeira pessoa. ":"")+"Y: "+label(TouchControlModel.Y)+". ":"Navegação de menus. ")+
            "Analógico à esquerda, câmera por arrasto à direita, ações A B X Y, pausa e opções.";
    }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); removeCallbacks(poll); post(poll); requestApplyInsets(); }
    @Override protected void onDetachedFromWindow() { removeCallbacks(poll); clear(); super.onDetachedFromWindow(); }
    @Override protected void onVisibilityChanged(View view,int visibility) {
        super.onVisibilityChanged(view,visibility);
        if(poll==null)return;
        removeCallbacks(poll);
        if(visibility==VISIBLE&&isAttachedToWindow())post(poll);
        else if(model!=null)clear();
    }
    @Override protected void onSizeChanged(int w,int h,int oldW,int oldH) { clear(); layoutModel(); }
    private void layoutModel() { model.layout(getWidth(),getHeight(),density,insetLeft,insetTop,insetRight,insetBottom); }
    boolean touchVisible() { return model.touchVisible; }
    void setTouchVisible(boolean value) { clear(); model.setTouchVisible(value); invalidate(); }
    float sensitivity() { return sensitivity; }
    boolean invertX() { return invertX; }
    boolean invertY() { return invertY; }
    void setCameraOptions(float value,boolean x,boolean y) {
        sensitivity=Math.max(.35f,Math.min(2,value)); invertX=x; invertY=y;
        getContext().getSharedPreferences("touch-controls",Context.MODE_PRIVATE).edit()
            .putFloat("sensitivity",sensitivity).putBoolean("invert-x",x).putBoolean("invert-y",y).apply();
    }
    void loadCameraOptions() {
        android.content.SharedPreferences prefs=getContext().getSharedPreferences("touch-controls",Context.MODE_PRIVATE);
        sensitivity=Math.max(.35f,Math.min(2,prefs.getFloat("sensitivity",1)));
        invertX=prefs.getBoolean("invert-x",false); invertY=prefs.getBoolean("invert-y",false);
    }
    void clear() { model.clear(); if(sink!=null&&sink.running())sink.clearTouch(); invalidate(); }
    private void sendTouch() { if(sink.running())sink.touch(model.buttons,model.lx,model.ly); }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action=event.getActionMasked(), changed=event.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL) { clear(); return true; }
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            int role=model.down(event.getPointerId(changed),event.getX(changed),event.getY(changed));
            if(role>=0&&role<TouchControlModel.COUNT)performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            if(getParent()!=null)getParent().requestDisallowInterceptTouchEvent(true);
        }
        for(int p=0;p<event.getPointerCount();++p) {
            if((action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP)&&p==changed)continue;
            model.move(event.getPointerId(p),event.getX(p),event.getY(p));
        }
        int released=TouchControlModel.NONE;
        if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP)
            released=model.up(event.getPointerId(changed),event.getX(changed),event.getY(changed));
        sendTouch();
        if(sink.running()&&(model.lookDx!=0||model.lookDy!=0)) {
            float edge=Math.max(1,Math.min(getWidth(),getHeight()));
            float dx=model.lookDx/edge*sensitivity*(invertX?-1:1)*(model.firstPerson?-1:1);
            float dy=model.lookDy/edge*sensitivity*(invertY?-1:1);
            sink.look(dx,dy);
        }
        model.lookDx=model.lookDy=0;
        invalidate();
        if(released==TouchControlModel.SETTINGS) { clear(); sink.settings(); }
        if(action==MotionEvent.ACTION_UP)performClick();
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if(model.touchVisible)drawStick(canvas);
        for(int b=0;b<TouchControlModel.COUNT;++b)if(model.visible[b])drawButton(canvas,b);
        if(model.cameraEnabled()&&!model.hasLooked) {
            paint.setStyle(Paint.Style.FILL); paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(Math.min(12*density,getHeight()*.025f));
            paint.setColor(0xbbe9f3ed);
            canvas.drawText("Arraste para olhar",getWidth()*.56f,getHeight()-Math.max(insetBottom+10*density,getHeight()*.045f),paint);
        }
    }
    private void drawStick(Canvas c) {
        float x=model.moveX,y=model.moveY,r=model.moveRadius;
        paint.setStyle(Paint.Style.FILL); paint.setColor(0x62212c34); c.drawCircle(x,y,r,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(Math.max(1.2f*density,r*.018f));
        paint.setColor(0xbdd8e4df); c.drawCircle(x,y,r,paint);
        paint.setColor(0x485eb6c9); c.drawCircle(x,y,r*.94f,paint);
        paint.setStyle(Paint.Style.FILL); paint.setColor(0xa5c4d3d2);
        for(int i=0;i<4;++i) {
            c.save(); c.rotate(i*90,x,y); path.reset(); path.moveTo(x,y-r*.87f);
            path.lineTo(x-r*.09f,y-r*.73f); path.lineTo(x+r*.09f,y-r*.73f); path.close(); c.drawPath(path,paint); c.restore();
        }
        float kx=x+model.lx*r*.54f,ky=y-model.ly*r*.54f;
        paint.setColor(model.movePointer!=-1?0xc471bfc9:0x99b9c9cd); c.drawCircle(kx,ky,r*.40f,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setColor(0xe4d9e7e5); c.drawCircle(kx,ky,r*.40f,paint);
    }
    private String label(int b) {
        if(!model.gameplay) return b==0?"OK":b==1?"VOLTAR":b==11?"SELECT":"";
        switch(b) {
        case 0:return model.character==1?(model.weapon>=2?"DISPARAR":model.weapon==1?"ATACAR":"AÇÃO"):model.character==2?"INVESTIDA":"CHUTE";
        case 1:return "PULAR";
        case 2:return model.character==1?(model.weapon>=2?"CARREGAR":model.weapon==1?"GOLPE":"SECUNDÁRIA"):model.character==2?"GIRO":"SOCO";
        case 3:return model.context==2?"PORTA":model.context==3?"EXAMINAR":model.context==4?"ACIONAR":"INTERAGIR";
        case 4:return "ESPECIAL";
        case 5:return model.character==1?"MIRAR":"CENTRAR";
        case 6:return "ARMA −"; case 7:return "ARMA +"; case 8:return "VISÃO"; case 12:return "ESQUIVA";
        default:return "";
        }
    }
    private void drawButton(Canvas c,int b) {
        float x=model.cx[b],y=model.cy[b],r=model.radius[b];
        boolean pressed=model.isPressed(b), contextual=b==3&&model.context!=0;
        int accent=contextual?GOLD:CYAN;
        paint.setStyle(Paint.Style.FILL); paint.setColor(pressed?0xcd395b62:0x9c1a252e); c.drawCircle(x,y,r,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(Math.max(1.1f*density,r*.022f));
        paint.setColor(pressed||contextual?accent:0xd8d7e0db); c.drawCircle(x,y,r,paint);
        paint.setColor(pressed?0x99d9f4ec:0x4469777c); c.drawCircle(x,y,r*.92f,paint);
        c.save(); c.translate(x,y-(b<5?r*.10f:0)); c.scale(r*.70f,r*.70f); drawIcon(c,b); c.restore();
        String caption=label(b);
        if(!caption.isEmpty()) {
            paint.setStyle(Paint.Style.FILL); paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(Math.min(10*density,getHeight()*.0185f));
            paint.setColor(CREAM);
            c.drawText(caption,x,Math.min(getHeight()-insetBottom-5*density,y+r+paint.getTextSize()),paint);
        }
        if(b<5) {
            float bx=x+r*.56f,by=y+r*.55f,br=r*(b==4?.31f:.26f);
            paint.setStyle(Paint.Style.FILL); paint.setColor(0xee142028); c.drawCircle(bx,by,br,paint);
            paint.setTextSize(br*(b==4?.90f:1.45f)); paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(b<4?BADGE[b]:GOLD);
            c.drawText(LETTER[b],bx,by-paint.ascent()*.34f,paint);
        }
    }
    private void stroke(float width,int color) { paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(width); paint.setColor(color); }
    private void fill(int color) { paint.setStyle(Paint.Style.FILL); paint.setColor(color); }
    private void line(Canvas c,float x1,float y1,float x2,float y2) { c.drawLine(x1,y1,x2,y2,paint); }
    private void drawIcon(Canvas c,int b) {
        stroke(.095f,CREAM);
        if(b==TouchControlModel.PAUSE) { line(c,-.27f,-.48f,-.27f,.48f); line(c,.27f,-.48f,.27f,.48f); return; }
        if(b==TouchControlModel.SETTINGS) { gear(c); return; }
        if(b==TouchControlModel.BACK) { line(c,.5f,-.4f,-.4f,0); line(c,-.4f,0,.5f,.4f); return; }
        if(b==TouchControlModel.PREVIOUS||b==TouchControlModel.NEXT) {
            float sign=b==TouchControlModel.NEXT?1:-1;
            line(c,-.5f*sign,0,.5f*sign,0); line(c,.1f*sign,-.4f,.5f*sign,0); line(c,.1f*sign,.4f,.5f*sign,0); return;
        }
        if(b==TouchControlModel.AIM) {
            c.drawCircle(0,0,.43f,paint); line(c,-.7f,0,-.25f,0); line(c,.25f,0,.7f,0);
            line(c,0,-.7f,0,-.25f); line(c,0,.25f,0,.7f); return;
        }
        if(b==TouchControlModel.VISION) {
            c.drawRoundRect(-.68f,-.38f,.68f,.43f,.13f,.13f,paint); c.drawCircle(0,.02f,.25f,paint);
            line(c,-.4f,-.4f,-.23f,-.57f); line(c,-.23f,-.57f,.2f,-.57f); return;
        }
        if(b==TouchControlModel.Y) {
            if(model.context==2) { door(c); return; }
            if(model.context==3) { c.drawCircle(-.12f,-.1f,.38f,paint); line(c,.17f,.2f,.62f,.62f); return; }
            if(model.context==4) { gear(c); return; }
            hand(c); return;
        }
        if(b==TouchControlModel.SPECIAL) { star(c,GOLD); return; }
        if(b==TouchControlModel.DODGE) { person(c); c.drawArc(-.8f,-.65f,.8f,.7f,140,200,false,paint); line(c,.72f,.35f,.78f,-.03f); return; }
        if(b==TouchControlModel.B) {
            if(!model.gameplay) { line(c,-.4f,-.4f,.4f,.4f); line(c,-.4f,.4f,.4f,-.4f); }
            else { person(c); line(c,.5f,.1f,.5f,-.65f); line(c,.5f,-.65f,.26f,-.35f); line(c,.5f,-.65f,.74f,-.35f); }
            return;
        }
        if(b==TouchControlModel.A&&!model.gameplay) { line(c,-.5f,0,-.1f,.4f); line(c,-.1f,.4f,.6f,-.45f); return; }
        if(model.character==TouchControlModel.FRY) {
            if(b==TouchControlModel.A) { if(model.weapon>=2)gun(c);else if(model.weapon==1)hammer(c);else fist(c); }
            else if(model.weapon!=1)star(c,CYAN); else { hammer(c); stroke(.07f,CYAN); c.drawArc(-.8f,-.8f,.8f,.8f,190,115,false,paint); }
        } else if(model.character==TouchControlModel.BENDER) {
            if(b==TouchControlModel.X) {
                c.drawArc(-.65f,-.58f,.65f,.58f,40,280,false,paint); line(c,.5f,-.37f,.65f,-.65f); line(c,.65f,-.65f,.25f,-.65f);
                c.drawCircle(0,0,.14f,paint);
            } else { fist(c); line(c,-.8f,.12f,-.52f,.12f); line(c,-.72f,.37f,-.45f,.37f); }
        } else if(model.character==TouchControlModel.LEELA) {
            if(b==TouchControlModel.X)fist(c);else boot(c);
        } else if(b==TouchControlModel.A)hammer(c);else star(c,CYAN);
    }
    private void person(Canvas c) {
        c.drawCircle(-.1f,-.50f,.12f,paint); line(c,-.1f,-.26f,-.26f,.12f);
        line(c,-.26f,.12f,-.58f,.51f); line(c,-.26f,.12f,.12f,.3f); line(c,.12f,.3f,.25f,.58f);
        line(c,-.12f,-.16f,-.56f,-.01f); line(c,-.12f,-.16f,.20f,-.32f);
    }
    private void gear(Canvas c) {
        c.drawCircle(0,0,.39f,paint); c.drawCircle(0,0,.13f,paint);
        for(int i=0;i<8;++i) { c.save(); c.rotate(i*45); line(c,0,-.42f,0,-.65f); c.restore(); }
    }
    private void door(Canvas c) {
        path.reset(); path.moveTo(-.53f,.62f); path.lineTo(-.53f,-.62f); path.lineTo(.38f,-.62f);
        path.lineTo(.38f,.62f); path.moveTo(-.48f,-.59f); path.lineTo(.12f,-.38f); path.lineTo(.12f,.63f);
        path.lineTo(-.48f,.42f); c.drawPath(path,paint); c.drawCircle(-.02f,.08f,.035f,paint);
    }
    private void hand(Canvas c) {
        path.reset(); path.moveTo(-.1f,.65f); path.lineTo(-.5f,.1f); path.quadTo(-.55f,-.08f,-.37f,-.03f);
        path.lineTo(-.14f,.2f); path.lineTo(-.14f,-.54f); path.quadTo(-.1f,-.75f,.05f,-.55f);
        path.lineTo(.05f,-.15f); path.quadTo(.23f,-.35f,.29f,-.12f); path.quadTo(.47f,-.29f,.51f,-.02f);
        path.quadTo(.72f,-.06f,.62f,.25f); path.lineTo(.4f,.65f); c.drawPath(path,paint);
        c.drawArc(-.5f,-.95f,.4f,-.12f,190,160,false,paint);
    }
    private void hammer(Canvas c) {
        c.save(); c.rotate(32); fill(0xffd9e5dc); c.drawRoundRect(-.10f,-.12f,.10f,.72f,.06f,.06f,paint);
        c.drawRoundRect(-.5f,-.59f,.5f,-.08f,.08f,.08f,paint); stroke(.06f,0xff12262e);
        c.drawRoundRect(-.5f,-.59f,.5f,-.08f,.08f,.08f,paint); line(c,-.3f,-.52f,-.3f,-.16f); c.restore();
    }
    private void gun(Canvas c) {
        fill(0xff8fc9b5); c.drawRoundRect(-.65f,-.36f,.45f,.06f,.1f,.1f,paint);
        path.reset(); path.moveTo(-.34f,.03f); path.lineTo(-.07f,.03f); path.lineTo(-.2f,.62f); path.lineTo(-.5f,.62f); path.close(); c.drawPath(path,paint);
        stroke(.07f,CREAM); c.drawRoundRect(-.65f,-.36f,.45f,.06f,.1f,.1f,paint); line(c,.43f,-.16f,.8f,-.16f);
        fill(GOLD); c.drawOval(.26f,-.38f,.54f,.07f,paint); stroke(.055f,0xff13272b); line(c,-.32f,-.26f,-.16f,-.26f);
    }
    private void fist(Canvas c) {
        fill(0xffdce6df); path.reset(); path.moveTo(-.50f,.14f); path.lineTo(-.53f,-.30f);
        path.quadTo(-.50f,-.60f,-.29f,-.45f); path.quadTo(-.12f,-.64f,.03f,-.46f);
        path.quadTo(.21f,-.57f,.32f,-.35f); path.quadTo(.59f,-.42f,.58f,-.05f);
        path.lineTo(.44f,.43f); path.lineTo(-.17f,.50f); path.close(); c.drawPath(path,paint);
        stroke(.06f,0xff26424a); line(c,-.26f,-.35f,-.22f,-.07f); line(c,.02f,-.35f,.05f,-.07f);
        line(c,.29f,-.28f,.29f,-.03f); line(c,-.35f,.07f,-.02f,.19f);
    }
    private void boot(Canvas c) {
        fill(CREAM); path.reset(); path.moveTo(-.16f,-.65f); path.lineTo(.21f,-.53f); path.lineTo(-.02f,.04f);
        path.lineTo(.56f,.20f); path.quadTo(.72f,.24f,.62f,.48f); path.lineTo(-.16f,.43f);
        path.lineTo(-.44f,.20f); path.close(); c.drawPath(path,paint); stroke(.07f,CYAN); line(c,-.31f,.28f,.56f,.41f);
    }
    private void star(Canvas c,int color) {
        fill(color); path.reset();
        for(int i=0;i<16;++i) {
            double angle=(i*2*Math.PI/16)-Math.PI/2; float r=i%2==0?.72f:.29f;
            float x=(float)Math.cos(angle)*r,y=(float)Math.sin(angle)*r;
            if(i==0)path.moveTo(x,y);else path.lineTo(x,y);
        }
        path.close(); c.drawPath(path,paint);
    }
}
