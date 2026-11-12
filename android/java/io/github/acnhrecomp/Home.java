// Quest launcher entry point. Keeps the game-folder picker, but always shows Play / Settings / Save Manager
// before entering NativeActivity. Save backups use Android's Storage Access Framework (no extra permissions).
package io.github.acnhrecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

public class Home extends Activity {
    static { System.loadLibrary("acnh"); }
    private static native String nativeLoadGame(String folder, String files);

    private static final int PICK = 1, EXPORT_SAVE = 2, IMPORT_SAVE = 3;
    private static final String GAME_LIB = "libacnh_game.so";
    private final boolean pt = Locale.getDefault().getLanguage().equals("pt");
    private LinearLayout box;
    private TextView message;
    private String waitingForAccess;
    private boolean busy;

    private String t(String en, String ptBr) { return pt ? ptBr : en; }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        String error = getIntent().getStringExtra("error");
        boolean ask = getIntent().getBooleanExtra("pick", false) || error != null;
        showLauncher(error != null ? explain(error) : null, ask);
        if (!ask && readSaved().isEmpty() && found().isEmpty()) pick();
    }

    @Override protected void onResume() {
        super.onResume();
        if (waitingForAccess != null && Environment.isExternalStorageManager()) {
            String f = waitingForAccess; waitingForAccess = null; check(f, false);
        }
    }

    private int dp(float v) { return Math.round(v * getResources().getDisplayMetrics().density); }

    private Button button(String label, int color, View.OnClickListener click) {
        Button b = new Button(this); b.setText(label); b.setAllCaps(false); b.setTextSize(18);
        b.setTextColor(color == Color.WHITE ? Color.rgb(92,70,50) : Color.WHITE);
        GradientDrawable bg = new GradientDrawable(); bg.setColor(color); bg.setCornerRadius(dp(24));
        b.setBackground(bg); b.setPadding(dp(22),dp(10),dp(22),dp(10)); b.setMinHeight(dp(56));
        b.setOnClickListener(click);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1,-2); lp.topMargin=dp(10); b.setLayoutParams(lp);
        return b;
    }

    private void base(String titleText, String subtitle, String note) {
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true);
        scroll.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{Color.rgb(160,222,200),Color.rgb(196,230,160)}));
        LinearLayout outer = new LinearLayout(this); outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(24),dp(24),dp(24),dp(24));
        box = new LinearLayout(this); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(28),dp(24),dp(28),dp(24));
        GradientDrawable paper = new GradientDrawable(); paper.setColor(Color.rgb(255,250,232)); paper.setCornerRadius(dp(28));
        box.setBackground(paper);
        outer.addView(box,new LinearLayout.LayoutParams(Math.min(dp(560),getResources().getDisplayMetrics().widthPixels-dp(48)),-2));
        scroll.addView(outer);
        TextView title = new TextView(this); title.setText(titleText); title.setTextSize(28);
        title.setTypeface(Typeface.DEFAULT_BOLD); title.setTextColor(Color.rgb(92,70,50)); box.addView(title);
        TextView desc = new TextView(this); desc.setText(subtitle); desc.setTextSize(16);
        desc.setTextColor(Color.rgb(120,100,80)); desc.setPadding(0,dp(8),0,dp(8)); box.addView(desc);
        message = new TextView(this); message.setTextSize(15); message.setTextColor(Color.rgb(190,80,80));
        message.setVisibility(note == null ? View.GONE : View.VISIBLE); if(note!=null) message.setText(note); box.addView(message);
        setContentView(scroll);
    }

    private void showLauncher(String note, boolean forcePick) {
        String saved = forcePick ? "" : readSaved();
        boolean valid = !saved.isEmpty() && hasPackage(new File(saved)) && canRead(saved);
        base("ACNH VR", t("Your game launcher","Seu inicializador"),
                note != null ? note : (valid ? t("Game folder ready.","Pasta do jogo pronta.")
                        : t("Choose your prepared game folder to get started.","Escolha a pasta preparada do jogo para começar.")));
        if (valid) box.addView(button(t("Play","Jogar"),Color.rgb(90,170,90),v -> check(saved,false)));
        else box.addView(button(t("Choose game folder","Escolher pasta do jogo"),Color.rgb(70,140,210),v -> pick()));
        box.addView(button(t("Settings","Configurações"),Color.rgb(110,145,190),v -> showSettings()));
        box.addView(button(t("Save Manager","Gerenciador de saves"),Color.rgb(190,145,75),v -> showSaveManager()));
        if (valid) box.addView(button(t("Change game folder","Trocar pasta do jogo"),Color.rgb(150,130,110),v -> pick()));
        for (final String f : found()) if (!valid || !f.equals(saved))
            box.addView(button(t("Use detected game folder","Usar pasta detectada"),Color.rgb(90,170,90),v -> check(f,false)));
    }

    private void showSettings() {
        base(t("Settings","Configurações"),t("Launcher settings","Configurações do inicializador"),null);
        String folder = readSaved();
        TextView current = new TextView(this); current.setText(t("Game folder: ","Pasta do jogo: ")+(folder.isEmpty()?t("Not selected","Não selecionada"):folder));
        current.setTextSize(14); current.setTextColor(Color.rgb(100,85,70)); current.setPadding(0,dp(12),0,dp(8)); box.addView(current);
        box.addView(button(t("Graphics","Gráficos"),Color.rgb(70,155,135),v -> showGraphics()));
        box.addView(button(t("Change game folder","Trocar pasta do jogo"),Color.rgb(70,140,210),v -> pick()));
        box.addView(button(t("Back","Voltar"),Color.rgb(150,130,110),v -> showLauncher(null,false)));
    }

    private String graphicsValue(String key, String fallback) {
        String folder = readSaved();
        if (folder.isEmpty()) return fallback;
        File ini = new File(folder, "config.ini");
        try {
            String section = "";
            for (String line : Files.readAllLines(ini.toPath(), StandardCharsets.UTF_8)) {
                String s = line.trim();
                if (s.isEmpty() || s.startsWith("#") || s.startsWith(";")) continue;
                if (s.startsWith("[") && s.endsWith("]")) { section = s.substring(1, s.length()-1).trim(); continue; }
                int eq = s.indexOf('=');
                if (eq < 0) continue;
                String full = section.isEmpty() ? s.substring(0,eq).trim() : section+"."+s.substring(0,eq).trim();
                if (full.equals(key)) return s.substring(eq+1).trim();
            }
        } catch (Exception ignored) {}
        return fallback;
    }

    private boolean writeGraphics(java.util.Map<String,String> changes) {
        String folder = readSaved();
        if (folder.isEmpty()) return false;
        File ini = new File(folder, "config.ini");
        try {
            List<String> lines = ini.isFile() ? Files.readAllLines(ini.toPath(), StandardCharsets.UTF_8) : new ArrayList<>();
            java.util.Set<String> written = new java.util.HashSet<>();
            String section = "";
            List<String> out = new ArrayList<>();
            for (String line : lines) {
                String s = line.trim();
                if (s.startsWith("[") && s.endsWith("]")) section = s.substring(1,s.length()-1).trim();
                int eq = s.indexOf('=');
                if (eq >= 0 && !s.startsWith("#") && !s.startsWith(";")) {
                    String key = section.isEmpty() ? s.substring(0,eq).trim() : section+"."+s.substring(0,eq).trim();
                    if (changes.containsKey(key)) {
                        out.add(line.substring(0, line.indexOf('=')+1) + " " + changes.get(key));
                        written.add(key);
                        continue;
                    }
                }
                out.add(line);
            }
            for (java.util.Map.Entry<String,String> e : changes.entrySet()) if (!written.contains(e.getKey())) {
                String key=e.getKey(); int dot=key.indexOf('.');
                String wanted=key.substring(0,dot), local=key.substring(dot+1);
                boolean hasSection=false;
                for (String line:out) if (line.trim().equals("["+wanted+"]")) {hasSection=true;break;}
                if (!hasSection) { if (!out.isEmpty() && !out.get(out.size()-1).isEmpty()) out.add(""); out.add("["+wanted+"]"); }
                int start=-1; for(int i=0;i<out.size();i++) if(out.get(i).trim().equals("["+wanted+"]")){start=i+1;break;}
                int end=out.size(); for(int i=start;i<out.size();i++) if(out.get(i).trim().startsWith("[")&&out.get(i).trim().endsWith("]")){end=i;break;}
                out.add(end,local+" = "+e.getValue());
            }
            Files.write(ini.toPath(),out,StandardCharsets.UTF_8);
            return true;
        } catch(Exception e) { return false; }
    }

    private void setGraphic(String key, String value) {
        java.util.Map<String,String> changes=new java.util.LinkedHashMap<>();
        changes.put(key,value);
        if (!writeGraphics(changes)) { say(t("Select a readable game folder before changing graphics settings.","Escolha uma pasta de jogo acessível antes de alterar os gráficos."),true); return; }
        showGraphics();
        say(t("Graphics setting saved. Restart the game to apply it.","Configuração gráfica salva. Reinicie o jogo para aplicar."),false);
    }

    private void setGraphicsPreset(String preset) {
        java.util.Map<String,String> changes=new java.util.LinkedHashMap<>();
        changes.put("video.preset",preset);
        if ("quality".equals(preset)) {
            changes.put("video.mode","docked"); changes.put("video.resolution","native"); changes.put("video.upscaler","linear"); changes.put("audio.reverb","1");
        } else if ("balanced".equals(preset)) {
            changes.put("video.mode","handheld"); changes.put("video.resolution","native"); changes.put("video.upscaler","fsr"); changes.put("audio.reverb","1");
        } else {
            changes.put("video.mode","docked"); changes.put("video.resolution","540"); changes.put("video.upscaler","fsr"); changes.put("video.frame_gen","off"); changes.put("audio.reverb","0");
        }
        if (!writeGraphics(changes)) { say(t("Select a readable game folder before changing graphics settings.","Escolha uma pasta de jogo acessível antes de alterar os gráficos."),true); return; }
        showGraphics();
        say(t("Graphics preset saved. Restart the game to apply it.","Predefinição gráfica salva. Reinicie o jogo para aplicar."),false);
    }

    private void showGraphics() {
        base(t("Graphics Settings","Configurações gráficas"),
                t("Adjust image quality and performance for the Quest 3. Changes are saved to the game folder's config.ini.","Ajuste qualidade e desempenho para o Quest 3. As alterações são salvas em config.ini."),null);
        if (readSaved().isEmpty() || !hasPackage(new File(readSaved()))) {
            say(t("Choose a game folder first.","Escolha uma pasta do jogo primeiro."),true);
            box.addView(button(t("Back","Voltar"),Color.rgb(150,130,110),v -> showSettings()));
            return;
        }
        String preset=graphicsValue("video.preset","custom");
        box.addView(button(t("Quality preset","Predefinição Qualidade")+(preset.equals("quality")?" ✓":""),Color.rgb(75,145,105),v -> setGraphicsPreset("quality")));
        box.addView(button(t("Balanced preset","Predefinição Equilibrada")+(preset.equals("balanced")?" ✓":""),Color.rgb(80,145,175),v -> setGraphicsPreset("balanced")));
        box.addView(button(t("Performance preset","Predefinição Desempenho")+(preset.equals("low")?" ✓":""),Color.rgb(180,135,65),v -> setGraphicsPreset("low")));
        box.addView(button(t("Resolution: ","Resolução: ")+graphicsValue("video.resolution","auto")+"  ·  "+t("Next","Próxima"),Color.rgb(110,145,190),v -> {
            String[] opts={"auto","native","720","900","1080","540"}; String cur=graphicsValue("video.resolution","auto"); int n=0;
            for(int i=0;i<opts.length;i++)if(opts[i].equals(cur)){n=(i+1)%opts.length;break;} setGraphic("video.resolution",opts[n]);
        }));
        box.addView(button(t("Upscaling: ","Escalonamento: ")+graphicsValue("video.upscaler","linear")+"  ·  "+t("Toggle","Alternar"),Color.rgb(110,145,190),v -> setGraphic("video.upscaler",graphicsValue("video.upscaler","linear").equals("fsr")?"linear":"fsr")));
        box.addView(button(t("Anti-aliasing: ","Antisserrilhamento: ")+graphicsValue("video.antialiasing","off")+"  ·  "+t("Toggle","Alternar"),Color.rgb(110,145,190),v -> setGraphic("video.antialiasing",graphicsValue("video.antialiasing","off").equals("fxaa")?"off":"fxaa")));
        box.addView(button(t("FPS counter: ","Contador FPS: ")+graphicsValue("video.show_fps","0")+"  ·  "+t("Toggle","Alternar"),Color.rgb(110,145,190),v -> setGraphic("video.show_fps",graphicsValue("video.show_fps","0").equals("1")?"0":"1")));
        box.addView(button(t("Back to Settings","Voltar às configurações"),Color.rgb(150,130,110),v -> showSettings()));
    }

    private void showSaveManager() {
        base(t("Save Manager","Gerenciador de saves"),
                t("Export a backup before reinstalling the app. Import replaces the current device save.","Exporte um backup antes de reinstalar o app. Importar substitui o save atual."),null);
        String dataRoot = readSaved();
        if (dataRoot.isEmpty() || !hasPackage(new File(dataRoot))) {
            TextView missing = new TextView(this);
            missing.setText(t("Select your game folder first. Save Manager will be available after the game folder is configured.",
                    "Escolha a pasta do jogo primeiro. O gerenciador de saves ficará disponível depois."));
            missing.setTextSize(15); missing.setTextColor(Color.rgb(100,85,70)); missing.setPadding(0,dp(12),0,dp(8));
            box.addView(missing);
            box.addView(button(t("Choose game folder","Escolher pasta do jogo"),Color.rgb(70,140,210),v -> pick()));
            box.addView(button(t("Back","Voltar"),Color.rgb(150,130,110),v -> showLauncher(null,false)));
            return;
        }
        File save = activeSave();
        TextView status = new TextView(this);
        status.setText(t("Save folder: ","Pasta do save: ")+save.getAbsolutePath()+"\n"+
                (save.isDirectory()?t("Save data found","Dados de save encontrados"):t("No save folder yet","Ainda não existe pasta de save")));
        status.setTextSize(13); status.setTextColor(Color.rgb(100,85,70)); status.setPadding(0,dp(10),0,dp(8)); box.addView(status);
        box.addView(button(t("Export save backup (.zip)","Exportar backup do save (.zip)"),Color.rgb(90,170,90),v -> {
            if (!save.isDirectory()) { say(t("No save data exists to export yet.","Ainda não há dados de save para exportar."),true); return; }
            Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/zip"); i.putExtra(Intent.EXTRA_TITLE,"ACNH-VR-save-backup.zip"); startActivityForResult(i,EXPORT_SAVE);
        }));
        box.addView(button(t("Import save backup (.zip)","Importar backup do save (.zip)"),Color.rgb(190,145,75),v -> {
            Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/zip"); startActivityForResult(i,IMPORT_SAVE);
        }));
        box.addView(button(t("Back","Voltar"),Color.rgb(150,130,110),v -> showLauncher(null,false)));
    }

    private void say(String text, boolean error) {
        if(message!=null){message.setTextColor(error?Color.rgb(190,80,80):Color.rgb(70,100,60));message.setText(text);message.setVisibility(View.VISIBLE);}
    }

    private String explain(String why) {
        if(why.startsWith("abi")) return t("The game in this folder was prepared for another app version. Prepare it again on the computer.","O jogo foi preparado para outra versão do app. Prepare novamente no computador.");
        if(why.startsWith("missing")) return t("This folder has no prepared game ("+GAME_LIB+"). Prepare it with ACNHRecomp-Importer and tools/android_install.sh.","Esta pasta não contém o jogo preparado ("+GAME_LIB+").");
        if(why.equals("restart")) return t("Close and reopen the app to use another folder.","Feche e abra o app novamente para usar outra pasta.");
        return why;
    }

    private static boolean hasPackage(File f) {
        return (new File(f,"game/android-arm64/"+GAME_LIB).isFile() || new File(f,GAME_LIB).isFile())
                && new File(f,"analysis/main.bin").isFile();
    }
    private List<String> found() {
        List<String> out=new ArrayList<>();
        for(File f:getExternalFilesDirs(null)) if(f!=null&&hasPackage(f)) out.add(f.getAbsolutePath());
        return out;
    }
    private boolean ownFolder(String path) {
        for(File f:getExternalFilesDirs(null)) if(f!=null&&path.startsWith(f.getAbsolutePath())) return true;
        return false;
    }
    private boolean canRead(String path) { return ownFolder(path)||Environment.isExternalStorageManager(); }
    private String readSaved() {
        try{return new String(Files.readAllBytes(new File(getFilesDir(),"data_root.txt").toPath()),StandardCharsets.UTF_8).trim();}
        catch(Exception e){return "";}
    }
    private File activeSave() { String root=readSaved(); return new File(new File(root,"user"),"device_save"); }

    private void pick() {
        try{startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE),PICK);}
        catch(Exception e){say(t("This device has no folder picker.","Não há seletor de pastas neste dispositivo."),true);}
    }
    private static String pathOf(Uri tree) {
        String id=DocumentsContract.getTreeDocumentId(tree); int colon=id.indexOf(':');
        String volume=colon<0?id:id.substring(0,colon), rest=colon<0?"":id.substring(colon+1);
        String base=volume.equals("primary")?Environment.getExternalStorageDirectory().getAbsolutePath():"/storage/"+volume;
        return rest.isEmpty()?base:base+"/"+rest;
    }

    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        if(request==PICK) {
            String path=pathOf(uri);
            if(!canRead(path)){waitingForAccess=path; say(t("Allow access to all files for ACNH Recomp, then return.","Permita acesso a todos os arquivos e volte."),false);
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,Uri.parse("package:"+getPackageName())));return;}
            check(path,false);
        } else if(request==EXPORT_SAVE) {
            final Uri target=uri; new Thread(()->{String err=null;try(OutputStream out=getContentResolver().openOutputStream(target);ZipOutputStream zip=new ZipOutputStream(new BufferedOutputStream(out))){zipTree(activeSave(),activeSave(),"",zip);}catch(Exception e){err=e.getMessage();}
                final String msg=err==null?t("Save backup exported.","Backup do save exportado."):t("Export failed: ","Falha ao exportar: ")+err;
                final boolean failed=err!=null;runOnUiThread(()->{showSaveManager();say(msg,failed);});}).start();
        } else if(request==IMPORT_SAVE) {
            new AlertDialog.Builder(this).setTitle(t("Replace current save?","Substituir o save atual?"))
                .setMessage(t("The ZIP backup will replace user/device_save. Export a backup first if you want to keep the current save.","O backup ZIP substituirá user/device_save. Exporte um backup primeiro para manter o save atual."))
                .setNegativeButton(t("Cancel","Cancelar"),null).setPositiveButton(t("Import","Importar"),(d,w)->importSave(uri)).show();
        }
    }

    private static void zipTree(File root,File file,String prefix,ZipOutputStream zip)throws Exception {
        File[] children=file.listFiles(); if(children==null)return;
        for(File child:children){String name=prefix+child.getName();
            if(child.isDirectory()){zip.putNextEntry(new ZipEntry(name+"/"));zip.closeEntry();zipTree(root,child,name+"/",zip);}
            else {zip.putNextEntry(new ZipEntry(name));try(InputStream in=new BufferedInputStream(new FileInputStream(child))){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)zip.write(b,0,n);}zip.closeEntry();}
        }
    }

    private void importSave(Uri uri) {
        new Thread(()->{String err=null;File save=activeSave(), parent=save.getParentFile(), stage=new File(parent,"device_save.importing"), old=new File(parent,"device_save.previous");
            try {
                deleteTree(stage); if(!parent.exists()&&!parent.mkdirs())throw new Exception("Cannot create save directory");
                try(InputStream in=getContentResolver().openInputStream(uri);ZipInputStream zip=new ZipInputStream(new BufferedInputStream(in))){
                    ZipEntry entry; byte[] buf=new byte[32768];
                    while((entry=zip.getNextEntry())!=null){String name=entry.getName();File out=new File(stage,name);
                        String canonical=out.getCanonicalPath(), base=stage.getCanonicalPath()+File.separator;
                        if(!canonical.startsWith(base))throw new Exception("Unsafe ZIP path");
                        if(entry.isDirectory()){if(!out.mkdirs()&&!out.isDirectory())throw new Exception("Cannot create folder");}
                        else {File p=out.getParentFile();if(!p.mkdirs()&&!p.isDirectory())throw new Exception("Cannot create folder");
                            try(OutputStream o=new BufferedOutputStream(new FileOutputStream(out))){int n;while((n=zip.read(buf))!=-1)o.write(buf,0,n);}}
                        zip.closeEntry();
                    }
                }
                if(!stage.isDirectory()||stage.list()==null||stage.list().length==0)throw new Exception("Backup is empty or invalid");
                deleteTree(old); if(save.exists()&&!save.renameTo(old))throw new Exception("Could not preserve existing save");
                if(!stage.renameTo(save)){if(old.exists())old.renameTo(save);throw new Exception("Could not install imported save");}
                deleteTree(old);
            } catch(Exception e){err=e.getMessage();deleteTree(stage);}
            final String msg=err==null?t("Save imported successfully.","Save importado com sucesso."):t("Import failed: ","Falha ao importar: ")+err;
            final boolean failed=err!=null;runOnUiThread(()->{showSaveManager();say(msg,failed);});
        }).start();
    }
    private static void deleteTree(File f){if(f.isDirectory()){File[] c=f.listFiles();if(c!=null)for(File x:c)deleteTree(x);}if(f.exists())f.delete();}

    private void check(final String folder,final boolean quiet) {
        if(busy)return;
        if(!hasPackage(new File(folder))){showLauncher(explain("missing")+"\n"+folder,true);return;}
        busy=true; if(!quiet){if(box==null)showLauncher(null,false);say(t("Preparing the game...","Preparando o jogo..."),false);
            ProgressBar p=new ProgressBar(this);p.setTag("progress");box.addView(p);}
        new Thread(()->{final String why=nativeLoadGame(folder,getFilesDir().getAbsolutePath());runOnUiThread(()->{
            busy=false;if(box!=null){View p=box.findViewWithTag("progress");if(p!=null)box.removeView(p);}
            if(!why.isEmpty()){showLauncher(explain(why),true);return;}
            try(FileOutputStream o=new FileOutputStream(new File(getFilesDir(),"data_root.txt"))){o.write((folder+"\n").getBytes(StandardCharsets.UTF_8));}catch(Exception ignored){}
            Intent game=new Intent();game.setClassName(getPackageName(),"android.app.NativeActivity");startActivity(game);
        });}).start();
    }
}
