package org.d2r.binaryprobe;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.drawable.ColorDrawable;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.io.IOException;

/** Native system-menu surface; the platform owns focus, Back and switch semantics. */
final class RendererSettingsDialog {
    interface ScaleRequest { int apply(float scale); }
    private final Activity activity;
    private final RendererOptionsStore store;
    private final RenderQualityStore qualityStore;
    private final ScaleRequest scaleRequest;
    private final RadioButton automatic,original,balanced,performance;
    private final TextView qualityStatus;
    private final Switch clear,writeback;
    private final TextView status;
    private final AlertDialog dialog;
    private RendererOptions desired;
    private boolean binding,readFailed;
    private String error;
    private int applied;
    private float appliedScale,desiredScale;
    private boolean qualityReadFailed;
    private String qualityError;
    private RendererSettingsDialog(Activity activity,int applied,float appliedScale,ScaleRequest scaleRequest,View trigger,Runnable closed){
        this.activity=activity;this.applied=applied;store=new RendererOptionsStore(activity);
        this.appliedScale=appliedScale;this.scaleRequest=scaleRequest;qualityStore=new RenderQualityStore(activity);
        LinearLayout body=new LinearLayout(activity);body.setOrientation(LinearLayout.VERTICAL);body.setPadding(dp(18),dp(8),dp(18),dp(12));
        text(body,"屏幕",18);
        Switch fillScreen=option(body,"铺满全屏","开启后游戏按手机屏幕比例原生渲染，不拉伸画面（切换后需重启游戏生效）；关闭则为 16:9 并留黑边。");
        fillScreen.setChecked(ScreenMode.fill(activity));
        fillScreen.setOnCheckedChangeListener((v,on)->{ScreenMode.setFill(activity,on);View vp=activity.findViewById(android.R.id.content);if(vp!=null)relayout(vp);});
        text(body,"按钮隐藏",18);
        RadioGroup hide=new RadioGroup(activity);hide.setOrientation(LinearLayout.HORIZONTAL);body.addView(hide);
        RadioButton[] modes={qualityOption(hide,"关闭"),qualityOption(hide,"淡出"),qualityOption(hide,"隐藏")};
        for(RadioButton m:modes)m.setLayoutParams(new RadioGroup.LayoutParams(0,-2,1));
        modes[ScreenMode.fadeMode(activity)].setChecked(true);
        text(body,"无操作一段时间后，淡出：屏幕上的按钮变淡；隐藏：按钮完全消失（设置按钮仍只淡出）。触摸后恢复。",14);
        TextView fadeLabel=text(body,"",14);SeekBar fadeBar=new SeekBar(activity);fadeBar.setMax(9);fadeBar.setProgress(ScreenMode.fadeSecs(activity)-1);
        body.addView(fadeBar,new LinearLayout.LayoutParams(-1,dp(40)));
        Runnable applyFade=()->{int s=fadeBar.getProgress()+1,m=0;for(int i=0;i<3;i++)if(modes[i].isChecked())m=i;
            fadeLabel.setText("等待时间："+s+" 秒");fadeBar.setEnabled(m!=0);ScreenMode.setFade(activity,m,s);};
        applyFade.run();hide.setOnCheckedChangeListener((g,id)->applyFade.run());
        fadeBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar b,int p,boolean user){applyFade.run();}
            public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){}});
        text(body,"系统信息",18);
        Switch info=option(body,"显示系统信息","在屏幕左上角显示帧率、CPU、GPU 和内存占用。CPU 为本进程占全部核心的比例，另给最忙线程；GPU 是按渲染耗时估算的占用，开启时才会统计。");
        info.setChecked(ScreenMode.info(activity));
        info.setOnCheckedChangeListener((v,on)->((GameActivity)activity).setSystemInfo(on));
        int qualityFrom=body.getChildCount();
        text(body,"画质模式",18);
        RadioGroup quality=new RadioGroup(activity);quality.setOrientation(LinearLayout.VERTICAL);body.addView(quality);
        automatic=qualityOption(quality,RenderQuality.description(RenderQuality.AUTO));
        original=qualityOption(quality,RenderQuality.description(RenderQuality.ORIGINAL));
        balanced=qualityOption(quality,RenderQuality.description(RenderQuality.BALANCED));
        performance=qualityOption(quality,RenderQuality.description(RenderQuality.PERFORMANCE));
        text(body,"自动模式在掉帧时动态降低 3D 分辨率；原画质固定 100%；均衡固定 90%；性能固定 75%。界面和文字保持当前输出分辨率。选择后自动保存并应用，切换时可能短暂停顿。",14);
        qualityStatus=text(body,"正在读取画质设置…",14);qualityStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        // Hidden until render scaling works in Hades II: the inherited D2R scale3d profile does not match this game (scale3d.supported is 0), so these modes do nothing.
        for(int i=qualityFrom;i<body.getChildCount();i++)body.getChildAt(i).setVisibility(View.GONE);
        text(body,"图形优化",18);
        clear=option(body,"清屏合并","将符合条件的整屏清除并入绘制，减少独立清除和显存读取。关闭后恢复原清除路径。");
        writeback=option(body,"省略无用写回","确认内容在读取前会被完整覆盖时，省掉这次显存保存。需要保留的内容仍正常保存。");
        text(body,"运行中生效并自动保存，可在同一场景往返对比。收益随场景变化，不保证提高帧率；若画面异常，可关闭对应项。",14);
        status=text(body,"正在读取设置…",14);status.setMinHeight(dp(48));status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        ScrollView scroll=new ScrollView(activity);scroll.setFillViewport(false);scroll.addView(body);
        dialog=new AlertDialog.Builder(activity).setTitle("系统菜单").setView(scroll).setPositiveButton("返回游戏",null).create();
        dialog.setOnDismissListener(d->{closed.run();if(!activity.isFinishing())trigger.requestFocus();});
        clear.setOnCheckedChangeListener((v,on)->change(RendererOptions.CLEAR_MERGE,on));
        writeback.setOnCheckedChangeListener((v,on)->change(RendererOptions.STORE_ELIDE,on));
        automatic.setOnClickListener(v->changeQuality(RenderQuality.AUTO));
        original.setOnClickListener(v->changeQuality(RenderQuality.ORIGINAL));
        balanced.setOnClickListener(v->changeQuality(RenderQuality.BALANCED));
        performance.setOnClickListener(v->changeQuality(RenderQuality.PERFORMANCE));
        try{desiredScale=qualityStore.read();bindQuality();}
        catch(IOException e){qualityReadFailed=true;qualityError="画质设置读取失败，请返回游戏后重新打开菜单。";automatic.setEnabled(false);original.setEnabled(false);balanced.setEnabled(false);performance.setEnabled(false);qualityFeedback();}
        reload();
    }
    static RendererSettingsDialog show(Activity activity,int applied,float scale,ScaleRequest request,View trigger,Runnable closed){
        RendererSettingsDialog panel=new RendererSettingsDialog(activity,applied,scale,request,trigger,closed);GameActivity.installDialogBack(panel.dialog);panel.dialog.show();
        panel.dialog.getWindow().setBackgroundDrawable(new ColorDrawable(0xff000000|TouchControlsView.INK));
        panel.dialog.getWindow().setLayout(Math.min(panel.dp(600),activity.getResources().getDisplayMetrics().widthPixels-panel.dp(36)),WindowManager.LayoutParams.WRAP_CONTENT);
        panel.dialog.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(TouchControlsView.TEXT);panel.automatic.requestFocus();return panel;
    }
    private static void relayout(View v){v.requestLayout();if(v instanceof android.view.ViewGroup){android.view.ViewGroup g=(android.view.ViewGroup)v;for(int i=0;i<g.getChildCount();i++)relayout(g.getChildAt(i));}}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    private TextView text(LinearLayout body,String value,int size){
        TextView view=new TextView(activity);view.setText(value);view.setTextColor(TouchControlsView.TEXT);view.setTextSize(size);
        view.setPadding(0,dp(4),0,dp(8));body.addView(view,new LinearLayout.LayoutParams(-1,-2));return view;
    }
    private RadioButton qualityOption(RadioGroup group,String label){
        RadioButton control=new RadioButton(activity);control.setId(View.generateViewId());control.setText(label);
        control.setTextColor(TouchControlsView.TEXT);control.setTextSize(17);control.setMinHeight(dp(48));
        control.setButtonTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{TouchControlsView.TEXT,TouchControlsView.RIM}));
        group.addView(control,new RadioGroup.LayoutParams(-1,-2));return control;
    }
    private void bindQuality(){
        automatic.setChecked(desiredScale==RenderQuality.AUTO);
        original.setChecked(desiredScale==RenderQuality.ORIGINAL);
        balanced.setChecked(desiredScale==RenderQuality.BALANCED);
        performance.setChecked(desiredScale==RenderQuality.PERFORMANCE);
        qualityFeedback();
    }
    private void changeQuality(float scale){
        if(qualityReadFailed)return;
        try{qualityStore.write(scale);desiredScale=scale;
            int rc=scaleRequest.apply(scale);qualityError=rc==0?null:"已保存，暂未应用。请再次选择模式重试。";
        }catch(IOException e){qualityError="保存失败，画质设置未改变。请重新选择重试。";}
        bindQuality();
    }
    private void qualityFeedback(){
        String value=qualityError!=null?qualityError:desiredScale==RenderQuality.AUTO?
            "自动模式已启用\n当前 3D 比例："+Math.round(appliedScale*100)+"%":
            Math.abs(appliedScale-desiredScale)<0.0001f?"已生效："+RenderQuality.description(appliedScale):
            "正在应用…\n当前生效："+RenderQuality.description(appliedScale);
        if(!value.contentEquals(qualityStatus.getText()))qualityStatus.setText(value);
    }
    private Switch option(LinearLayout body,String title,String detail){
        Switch control=new Switch(activity);control.setText(title);control.setTextColor(TouchControlsView.TEXT);control.setTextSize(17);
        control.setMinHeight(dp(48));control.setSwitchPadding(dp(18));control.setThumbTintList(ColorStateList.valueOf(TouchControlsView.TEXT));
        control.setTrackTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{TouchControlsView.PRESSED,TouchControlsView.RIM}));
        body.addView(control,new LinearLayout.LayoutParams(-1,-2));text(body,detail,14);return control;
    }
    private void reload(){
        try{desired=store.read();readFailed=false;error=null;bind();}
        catch(IOException e){readFailed=true;error="设置读取失败，返回游戏后重新打开菜单重试。";clear.setEnabled(false);writeback.setEnabled(false);feedback();}
    }
    private void bind(){
        binding=true;clear.setChecked(desired.enabled(RendererOptions.CLEAR_MERGE));writeback.setChecked(desired.enabled(RendererOptions.STORE_ELIDE));binding=false;feedback();
    }
    private void change(int bit,boolean enabled){
        if(binding||readFailed)return;
        try{desired=store.change(bit,enabled);error=null;bind();}
        catch(IOException e){error="保存失败，设置未改变。请重新切换重试。";bind();}
    }
    void updateApplied(int flags,float scale){
        applied=flags;
        appliedScale=scale;qualityFeedback();
        if(!readFailed)try{RendererOptions latest=store.read();if(desired==null||latest.sequence!=desired.sequence){desired=latest;error=null;bind();}}
        catch(IOException e){error="设置暂时无法读取，请返回游戏后重试。";}
        feedback();
    }
    private void feedback(){
        String value=error!=null?error:applied==desired.flags?"已生效："+RendererOptions.description(applied):"正在应用…\n当前生效："+RendererOptions.description(applied);
        if(!value.contentEquals(status.getText()))status.setText(value);
    }
    void dismiss(){dialog.dismiss();}
}
