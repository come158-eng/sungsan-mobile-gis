// Modified for Sungsan Mobile GIS by Sungsan on 2026-08-07.
// Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
/**
 * QFieldActivity.java - class needed to copy files from assets to
 * getExternalFilesDir() before starting QtActivity this can be used to perform
 * actions before QtActivity takes over.
 * @author  Marco Bernasocchi - <marco@opengis.ch>
 */
/*
 Copyright (c) 2011, Marco Bernasocchi <marco@opengis.ch>
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of the  Marco Bernasocchi <marco@opengis.ch> nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Marco Bernasocchi <marco@opengis.ch> ''AS IS'' AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Marco Bernasocchi <marco@opengis.ch> BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package ch.opengis.qfield;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.Insets;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.provider.Settings;
import android.text.Html;
import android.text.InputType;
import android.util.Base64;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.WindowManager.LayoutParams;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.content.FileProvider;
import androidx.documentfile.provider.DocumentFile;
import ch.opengis.qfield.QFieldUtils;
import ch.opengis.qfield.R;
import io.sentry.android.core.SentryAndroid;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.Thread;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.MessageDigest;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Enumeration;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.qtproject.qt.android.bindings.QtActivity;

public class QFieldActivity extends QtActivity {

    private static final int IMPORT_DATASET = 300;
    private static final int IMPORT_PROJECT_FOLDER = 301;
    private static final int IMPORT_PROJECT_ARCHIVE = 302;
    private static final int IMPORT_LANDSTAR_POINTS = 303;

    private static final int UPDATE_PROJECT_FROM_ARCHIVE = 400;

    private static final int EXPORT_TO_FOLDER = 500;

    private SharedPreferences sharedPreferences;
    private SharedPreferences.Editor sharedPreferenceEditor;
    private ProgressDialog progressDialog;
    ExecutorService executorService = Executors.newFixedThreadPool(4);

    public static native void openProject(String url);
    public static native boolean clearProject();

    public static native void openPath(String path);
    public static native void executeAction(String action);

    public static native void volumeKeyDown(int volumeKeyCode);
    public static native void volumeKeyUp(int volumeKeyCode);

    public static native void resourceReceived(String path);
    public static native void resourceOpened(String path);
    public static native void resourceCanceled(String message);
    public static native void landStarFileReceived(String path);

    private static final String SUNGSAN_PACKAGE_ID = "kr.co.sungsan.mobilegis";
    private static final String SUNGSAN_ACTIVATION_PREFERENCES =
        "sungsan_activation_v1";
    private static final String SUNGSAN_ACTIVATION_TOKEN = "activation_token";
    private static final String SUNGSAN_ACTIVATION_PUBLIC_KEY =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6lgfKMb/3AOh3sC8xCFtOj7bhBOkY72HBOEeMgfTE2zaAWVvtrrCFuWSBLTA6C8REECtBzDBo28G/Imb7fbT4A==";

    private AlertDialog sungsanActivationDialog;

    private Intent projectIntent;
    private Intent qfieldIntent;

    private float originalBrightness;
    private boolean handleVolumeKeys = false;
    private String pathsToExport;
    private String projectPath;

    private static final int CAMERA_RESOURCE = 600;
    private static final int GALLERY_RESOURCE = 601;
    private static final int FILE_PICKER_RESOURCE = 602;
    private static final int OPEN_RESOURCE = 603;
    private String resourcePrefix;
    private String resourceFilePath;
    private String resourceSuffix;
    private String resourceTempFilePath;
    private File resourceFile;
    private File resourceCacheFile;
    private boolean resourceIsEditing;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        prepareQtActivity();
        super.onCreate(savedInstanceState);

        View decorView = getWindow().getDecorView();
        decorView.getViewTreeObserver().addOnGlobalLayoutListener(
            new ViewTreeObserver.OnGlobalLayoutListener() {
                @Override
                public void onGlobalLayout() {
                    if (android.os.Build.VERSION.SDK_INT >=
                        android.os.Build.VERSION_CODES.R) {
                        WindowInsets insets = decorView.getRootWindowInsets();
                        if (insets != null &&
                            !insets.isVisible(WindowInsets.Type.ime())) {
                            decorView.requestLayout();
                        }
                    }
                }
            });

        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            decorView.post(new Runnable() {
                @Override
                public void run() {
                    showSungsanActivationDialog();
                }
            });
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Re-check finite administrator tokens whenever the app returns to the
        // foreground. Permanent (expiresAt=0) approvals remain a one-time
        // activation, while an expired temporary approval cannot keep using an
        // already-open activity indefinitely.
        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            getWindow().getDecorView().post(new Runnable() {
                @Override
                public void run() {
                    showSungsanActivationDialog();
                }
            });
        }
    }

    @Override
    public void onNewIntent(Intent intent) {
        // Prevent activity restart
        intent.setFlags(intent.getFlags() &
                        ~(Intent.FLAG_ACTIVITY_NEW_TASK |
                          Intent.FLAG_ACTIVITY_NEW_DOCUMENT));
        super.onNewIntent(intent);

        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            showSungsanActivationDialog();
            return;
        }

        if (Intent.ACTION_VIEW.equals(intent.getAction()) ||
            Intent.ACTION_SEND.equals(intent.getAction())) {
            String scheme = intent.getScheme();
            if (scheme != null && scheme.equals("qfield")) {
                qfieldIntent = intent;
                processQFieldIntent();
            } else if (scheme != null && scheme.equals("https")) {
                Uri uri = intent.getData();
                String host = uri.getHost();
                if (host.equals("qfield.org")) {
                    qfieldIntent = intent;
                    processQFieldIntent();
                }
            } else {
                projectIntent = intent;
                processProjectIntent();
            }
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (handleVolumeKeys && (keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
                                 keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
                                 keyCode == KeyEvent.KEYCODE_MUTE)) {
            // Forward volume keys' presses to QField
            volumeKeyDown(keyCode);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (handleVolumeKeys && (keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
                                 keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
                                 keyCode == KeyEvent.KEYCODE_MUTE)) {
            // Forward volume keys's releases to QField
            volumeKeyUp(keyCode);
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    private boolean isDarkTheme() {
        switch (getResources().getConfiguration().uiMode &
                Configuration.UI_MODE_NIGHT_MASK) {
            case Configuration.UI_MODE_NIGHT_YES:
                return true;

            case Configuration.UI_MODE_NIGHT_NO:
                return false;
        }
        return false;
    }

    private void vibrate(int milliseconds) {
        Vibrator v = (Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        v.vibrate(VibrationEffect.createOneShot(
            milliseconds, VibrationEffect.DEFAULT_AMPLITUDE));
    }

    private void processQFieldIntent() {
        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            showSungsanActivationDialog();
            return;
        }
        String data = qfieldIntent.getDataString();
        executeAction(data);
    }

    private void processProjectIntent() {
        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            showSungsanActivationDialog();
            return;
        }
        showBlockingProgressDialog(getString(R.string.processing_message));

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                final Intent incomingIntent = projectIntent;
                if (incomingIntent == null) {
                    dismissBlockingProgressDialog();
                    return;
                }

                String scheme = incomingIntent.getScheme();
                String action = incomingIntent.getAction();
                String type = incomingIntent.getType();
                Context context = getApplication().getApplicationContext();

                Uri uri = null;
                if (Intent.ACTION_SEND.equals(action)) {
                    uri = (Uri)incomingIntent.getParcelableExtra(
                        Intent.EXTRA_STREAM);
                    if (uri == null) {
                        uri = incomingIntent.getData();
                    }
                    if (uri == null && incomingIntent.getClipData() != null &&
                        incomingIntent.getClipData().getItemCount() > 0) {
                        uri = incomingIntent.getClipData().getItemAt(0).getUri();
                    }
                    scheme = "";
                } else {
                    uri = incomingIntent.getData();
                }

                if (uri == null) {
                    dismissBlockingProgressDialog();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (!isFinishing()) {
                                displayAlertDialog(
                                    getString(R.string.import_error),
                                    "공유된 파일을 확인하지 못했습니다.");
                            }
                        }
                    });
                    return;
                }

                String filePath = QFieldUtils.getPathFromUri(context, uri);
                if (filePath == null) {
                    filePath = "";
                }
                String importDatasetPath = "";
                String importProjectPath = "";
                File externalFilesDir = getApplicationDir();
                if (externalFilesDir != null) {
                    importDatasetPath = externalFilesDir.getAbsolutePath() +
                                        "/Imported Datasets/";
                    new File(importDatasetPath).mkdir();
                    importProjectPath = externalFilesDir.getAbsolutePath() +
                                        "/Imported Projects/";
                    new File(importProjectPath).mkdir();
                }

                if ((ContentResolver.SCHEME_CONTENT.equals(scheme) ||
                     Intent.ACTION_SEND.equals(action)) &&
                    !importDatasetPath.isEmpty()) {
                    DocumentFile documentFile =
                        DocumentFile.fromSingleUri(context, uri);
                    String fileName =
                        documentFile != null ? documentFile.getName() : null;
                    long fileBytes =
                        documentFile != null ? documentFile.length() : -1L;
                    if (fileName == null) {
                        String uriName = uri.getLastPathSegment();
                        if (uriName != null && !uriName.trim().isEmpty()) {
                            fileName = uriName;
                        } else if (type != null) {
                            // File name not provided
                            fileName =
                                new SimpleDateFormat("yyyyMMdd_HHmmss")
                                    .format(new Date().getTime()) +
                                "." +
                                QFieldUtils.getExtensionFromMimeType(type);
                        }
                    }

                    fileName = safeLeafName(fileName);

                    if (fileName != null) {
                        if (isLandStarPointFile(fileName) ||
                            isLandStarMimeType(type)) {
                            final Uri landStarUri = uri;
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    dismissBlockingProgressDialog();
                                    importLandStarUri(landStarUri);
                                }
                            });
                            return;
                        }
                        String fileBaseName = fileName;
                        String fileExtension = "";
                        if (fileName.lastIndexOf(".") > -1) {
                            fileBaseName = fileName.substring(
                                0, fileName.lastIndexOf("."));
                            fileExtension =
                                fileName.substring(fileName.lastIndexOf("."));
                        }

                        ContentResolver resolver = getContentResolver();
                        if (type != null && type.equals("application/zip")) {
                            String projectName = "";
                            try (InputStream input =
                                     resolver.openInputStream(uri)) {
                                projectName =
                                    QFieldUtils.getArchiveProjectName(input);
                            } catch (Exception e) {
                                Log.e("QField",
                                      "Could not inspect shared project ZIP.",
                                      e);
                            }

                            String projectFolderName = documentFile != null
                                                           ? projectFolderNameForArchive(
                                                                 documentFile)
                                                           : "";
                            if (!projectName.isEmpty() &&
                                !projectFolderName.isEmpty()) {
                                String projectPath =
                                    importProjectPath + projectFolderName + "/";
                                int i = 1;
                                while (new File(projectPath).exists()) {
                                    projectPath = importProjectPath +
                                                  projectFolderName + "_" + i +
                                                  "/";
                                    i++;
                                }
                                new File(projectPath).mkdir();
                                boolean imported = false;
                                try (InputStream input =
                                         resolver.openInputStream(uri)) {
                                    imported =
                                        input != null &&
                                        QFieldUtils.zipToFolder(input,
                                                                projectPath);
                                } catch (Exception e) {
                                    Log.e("QField",
                                          "Could not import shared project ZIP.",
                                          e);
                                }
                                imported =
                                    imported &&
                                    new File(projectPath, projectName).isFile();

                                final boolean importSucceeded = imported;
                                final String importedProjectPath =
                                    projectPath + projectName;
                                final File importedFolder =
                                    new File(projectPath);
                                if (!importSucceeded &&
                                    !deletePath(importedFolder)) {
                                    Log.w(
                                        "QField",
                                        "Could not remove failed shared ZIP import.");
                                }
                                runOnUiThread(new Runnable() {
                                    @Override
                                    public void run() {
                                        dismissBlockingProgressDialog();
                                        if (importSucceeded) {
                                            Log.v(
                                                "QField",
                                                "Opening decompressed project: " +
                                                    importedProjectPath);
                                            openProject(importedProjectPath);
                                        } else if (!isFinishing()) {
                                            displayAlertDialog(
                                                getString(
                                                    R.string.import_error),
                                                getString(
                                                    R.string
                                                        .import_project_archive_error));
                                        }
                                    }
                                });
                                return;
                            }

                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    dismissBlockingProgressDialog();
                                    if (!isFinishing()) {
                                        displayAlertDialog(
                                            getString(R.string.import_error),
                                            getString(
                                                R.string
                                                    .import_project_archive_error));
                                    }
                                }
                            });
                            return;
                        }

                        Boolean canWrite = !filePath.isEmpty()
                                               ? new File(filePath).canWrite()
                                               : false;
                        if (!canWrite) {
                            Log.v("QField",
                                  "Content intent detected: " + action + " : " +
                                      incomingIntent.getDataString() + " : " +
                                      type + " : " + fileName);
                            String importFilePath =
                                importDatasetPath + fileName;
                            int i = 1;
                            while (new File(importFilePath).exists()) {
                                importFilePath = importDatasetPath +
                                                 fileBaseName + "_" + i +
                                                 fileExtension;
                                i++;
                            }
                            Log.v("QField",
                                  "Importing document to file path: " +
                                      importFilePath);
                            try {
                                InputStream input =
                                    resolver.openInputStream(uri);
                                QFieldUtils.inputStreamToFile(
                                    input, importFilePath, fileBytes);
                            } catch (Exception e) {
                                e.printStackTrace();
                            }
                            dismissBlockingProgressDialog();
                            openProject(importFilePath);
                            return;
                        }
                    }
                }

                Log.v("QField", "Opening document file path: " + filePath);
                dismissBlockingProgressDialog();
                if (isLandStarPointFile(filePath)) {
                    landStarFileReceived(filePath);
                    return;
                }
                if (!filePath.isEmpty()) {
                    openProject(filePath);
                }
            }
        });
    }

    private boolean requiresSungsanActivation() {
        return SUNGSAN_PACKAGE_ID.equals(getPackageName());
    }

    private String sungsanRequestCode() {
        try {
            String androidId = Settings.Secure.getString(
                getContentResolver(), Settings.Secure.ANDROID_ID);
            if (androidId == null || androidId.trim().isEmpty()) {
                androidId = "unknown-device";
            }
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] value = digest.digest(
                (getPackageName() + "|" + androidId)
                    .getBytes(StandardCharsets.UTF_8));
            StringBuilder result = new StringBuilder("SS");
            for (int i = 0; i < 10; i++) {
                if (i % 2 == 0) {
                    result.append('-');
                }
                result.append(String.format("%02X", value[i] & 0xff));
            }
            return result.toString();
        } catch (Exception error) {
            Log.e("SungsanActivation",
                  "Could not create device request code.", error);
            return "SS-REQUEST-CODE-ERROR";
        }
    }

    private boolean hasValidSungsanActivation() {
        String token = getSharedPreferences(
                           SUNGSAN_ACTIVATION_PREFERENCES,
                           Context.MODE_PRIVATE)
                           .getString(SUNGSAN_ACTIVATION_TOKEN, "");
        return verifySungsanActivation(token);
    }

    private boolean verifySungsanActivation(String token) {
        try {
            if (token == null) {
                return false;
            }
            String compact = token.replaceAll("\\s+", "");
            String[] tokenParts = compact.split("\\.", -1);
            if (tokenParts.length != 2 || tokenParts[0].isEmpty() ||
                tokenParts[1].isEmpty()) {
                return false;
            }

            byte[] payload = Base64.decode(
                tokenParts[0],
                Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
            byte[] signatureBytes = Base64.decode(
                tokenParts[1],
                Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
            String[] payloadParts =
                new String(payload, StandardCharsets.UTF_8).split("\\|", -1);
            if (payloadParts.length != 5 ||
                !"SS1".equals(payloadParts[0]) ||
                !sungsanRequestCode().equals(payloadParts[1])) {
                return false;
            }

            long issuedAt = Long.parseLong(payloadParts[2]);
            long expiresAt = Long.parseLong(payloadParts[3]);
            long now = System.currentTimeMillis() / 1000L;
            if (issuedAt <= 0 || issuedAt > now + 86400L ||
                (expiresAt != 0L && expiresAt < now)) {
                return false;
            }

            byte[] publicKeyBytes =
                Base64.decode(SUNGSAN_ACTIVATION_PUBLIC_KEY, Base64.DEFAULT);
            KeyFactory keyFactory = KeyFactory.getInstance("EC");
            Signature verifier = Signature.getInstance("SHA256withECDSA");
            verifier.initVerify(keyFactory.generatePublic(
                new X509EncodedKeySpec(publicKeyBytes)));
            verifier.update(payload);
            return verifier.verify(signatureBytes);
        } catch (Exception error) {
            Log.w("SungsanActivation", "Activation token rejected.", error);
            return false;
        }
    }

    private void showSungsanActivationDialog() {
        if (!requiresSungsanActivation() || hasValidSungsanActivation() ||
            isFinishing()) {
            return;
        }
        if (sungsanActivationDialog != null &&
            sungsanActivationDialog.isShowing()) {
            return;
        }

        final String requestCode = sungsanRequestCode();
        final int padding =
            (int)(20 * getResources().getDisplayMetrics().density);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, padding / 2, padding, 0);

        TextView instructions = new TextView(this);
        instructions.setText(
            "이 휴대폰은 최초 1회 관리자 승인이 필요합니다.\n" +
            "아래 설치 요청번호를 관리자에게 보내고 승인번호를 받으세요.\n\n" +
            "관리자: 공도원\n문의: come158@naver.com");
        instructions.setTextSize(15);
        content.addView(instructions);

        TextView requestView = new TextView(this);
        requestView.setText("\n설치 요청번호\n" + requestCode);
        requestView.setTextSize(18);
        requestView.setTextIsSelectable(true);
        requestView.setPadding(0, padding / 2, 0, padding / 2);
        content.addView(requestView);

        Button copyButton = new Button(this);
        copyButton.setText("설치 요청번호 복사");
        copyButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                ClipboardManager clipboard = (ClipboardManager)getSystemService(
                    Context.CLIPBOARD_SERVICE);
                if (clipboard != null) {
                    clipboard.setPrimaryClip(
                        ClipData.newPlainText("성산 GIS 설치 요청번호",
                                              requestCode));
                }
            }
        });
        content.addView(copyButton);

        final EditText tokenInput = new EditText(this);
        tokenInput.setHint("관리자에게 받은 활성화 번호 붙여넣기");
        tokenInput.setInputType(InputType.TYPE_CLASS_TEXT |
                                InputType.TYPE_TEXT_FLAG_MULTI_LINE |
                                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        tokenInput.setMinLines(3);
        tokenInput.setSelectAllOnFocus(true);
        content.addView(tokenInput);

        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("성산 모바일 GIS 사용 승인");
        builder.setView(content);
        builder.setCancelable(false);
        builder.setPositiveButton("사용 승인", null);
        builder.setNegativeButton(
            "앱 종료", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    finishAffinity();
                }
            });
        sungsanActivationDialog = builder.create();
        sungsanActivationDialog.setCanceledOnTouchOutside(false);
        sungsanActivationDialog.setOnShowListener(
            new DialogInterface.OnShowListener() {
                @Override
                public void onShow(DialogInterface ignored) {
                    Button approve = sungsanActivationDialog.getButton(
                        AlertDialog.BUTTON_POSITIVE);
                    approve.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View view) {
                            String token = tokenInput.getText().toString().trim();
                            if (!verifySungsanActivation(token)) {
                                tokenInput.setError(
                                    "승인번호가 이 휴대폰과 일치하지 않거나 만료됐습니다.");
                                return;
                            }
                            getSharedPreferences(
                                SUNGSAN_ACTIVATION_PREFERENCES,
                                Context.MODE_PRIVATE)
                                .edit()
                                .putString(SUNGSAN_ACTIVATION_TOKEN,
                                           token.replaceAll("\\s+", ""))
                                .apply();
                            sungsanActivationDialog.dismiss();
                        }
                    });
                }
            });
        sungsanActivationDialog.show();
        if (sungsanActivationDialog.getWindow() != null) {
            sungsanActivationDialog.getWindow().addFlags(
                WindowManager.LayoutParams.FLAG_SECURE);
        }
    }

    private static boolean isLandStarPointFile(String pathOrName) {
        if (pathOrName == null) {
            return false;
        }
        String name = pathOrName.toLowerCase();
        return name.endsWith(".txt") || name.endsWith(".csv") ||
               name.endsWith(".pxy") || name.endsWith(".kof");
    }

    private static boolean isLandStarMimeType(String mimeType) {
        if (mimeType == null) {
            return false;
        }
        return mimeType.equalsIgnoreCase("text/plain") ||
               mimeType.equalsIgnoreCase("text/csv") ||
               mimeType.equalsIgnoreCase("application/csv");
    }

    private static String safeLeafName(String fileName) {
        String leaf = fileName == null ? "" : new File(fileName).getName();
        leaf = leaf.replaceAll("[\\x00-\\x1f<>:\"/\\\\|?*]", "_")
                   .trim();
        if (leaf.isEmpty() || leaf.equals(".") || leaf.equals("..")) {
            leaf = "landstar_points.txt";
        }
        if (leaf.length() > 180) {
            int dot = leaf.lastIndexOf('.');
            String extension = dot >= 0 ? leaf.substring(dot) : "";
            leaf = leaf.substring(0, Math.min(160, dot >= 0 ? dot : leaf.length())) +
                   extension;
        }
        return leaf;
    }

    private File uniqueLandStarInboxFile(String fileName) {
        File applicationDir = getApplicationDir();
        if (applicationDir == null) {
            return null;
        }
        File inbox = new File(applicationDir, "LandStar Inbox");
        if (!inbox.exists() && !inbox.mkdirs()) {
            return null;
        }
        String safeName = safeLeafName(fileName);
        int dot = safeName.lastIndexOf('.');
        String base = dot > 0 ? safeName.substring(0, dot) : safeName;
        String extension = dot > 0 ? safeName.substring(dot) : "";
        File destination = new File(inbox, safeName);
        int suffix = 1;
        while (destination.exists()) {
            destination = new File(
                inbox, base + "_" + suffix + extension);
            suffix++;
        }
        return destination;
    }

    /**
     * Copies one bounded LandStar text file, preserves the exact raw bytes in
     * a private sidecar for survey evidence, and normalizes Korean Windows-949
     * input to UTF-8. C++ parses only the validated UTF-8 file.
     */
    private boolean copyLandStarPointFile(InputStream input, File destination,
                                          long declaredLength) {
        final int maximumBytes = 25 * 1024 * 1024;
        if (input == null || destination == null ||
            declaredLength > maximumBytes) {
            return false;
        }
        try {
            final int initialCapacity =
                declaredLength > 0
                    ? (int)Math.min(declaredLength, 1024L * 1024L)
                    : 64 * 1024;
            ByteArrayOutputStream buffer = new ByteArrayOutputStream(
                initialCapacity);
            byte[] chunk = new byte[64 * 1024];
            int total = 0;
            int read;
            while ((read = input.read(chunk)) != -1) {
                total += read;
                if (total > maximumBytes) {
                    return false;
                }
                buffer.write(chunk, 0, read);
            }
            if (total == 0) {
                return false;
            }
            byte[] raw = buffer.toByteArray();
            CharsetDecoder utf8 = StandardCharsets.UTF_8.newDecoder()
                                      .onMalformedInput(CodingErrorAction.REPORT)
                                      .onUnmappableCharacter(CodingErrorAction.REPORT);
            String text;
            try {
                text = utf8.decode(ByteBuffer.wrap(raw)).toString();
            } catch (CharacterCodingException invalidUtf8) {
                CharsetDecoder cp949 = Charset.forName("MS949")
                                           .newDecoder()
                                           .onMalformedInput(CodingErrorAction.REPORT)
                                           .onUnmappableCharacter(CodingErrorAction.REPORT);
                text = cp949.decode(ByteBuffer.wrap(raw)).toString();
            }
            if (text.indexOf('\u0000') >= 0) {
                return false;
            }
            if (text.startsWith("\uFEFF")) {
                text = text.substring(1);
            }
            File rawDestination = new File(
                destination.getAbsolutePath() + ".source");
            if (rawDestination.exists() && !rawDestination.delete()) {
                return false;
            }
            try (FileOutputStream rawOutput =
                     new FileOutputStream(rawDestination)) {
                rawOutput.write(raw);
                rawOutput.flush();
                rawOutput.getFD().sync();
            }
            try (FileOutputStream output = new FileOutputStream(destination)) {
                output.write(text.getBytes(StandardCharsets.UTF_8));
                output.flush();
                output.getFD().sync();
            }
            if (!destination.isFile() || destination.length() <= 0 ||
                !rawDestination.isFile() || rawDestination.length() != raw.length) {
                destination.delete();
                rawDestination.delete();
                return false;
            }
            return true;
        } catch (Exception error) {
            destination.delete();
            new File(destination.getAbsolutePath() + ".source").delete();
            Log.e("SungsanLandStar",
                  "Could not validate LandStar point file.", error);
            return false;
        }
    }

    private void importLandStarUri(final Uri uri) {
        if (uri == null) {
            return;
        }
        final Context context = getApplication().getApplicationContext();
        final ContentResolver resolver = getContentResolver();
        final DocumentFile documentFile =
            DocumentFile.fromSingleUri(context, uri);
        String displayName =
            documentFile != null ? documentFile.getName() : null;
        if ((displayName == null || displayName.trim().isEmpty()) &&
            uri.getLastPathSegment() != null) {
            displayName = uri.getLastPathSegment();
        }
        final String mimeType = resolver.getType(uri);
        if (!isLandStarPointFile(displayName) &&
            isLandStarMimeType(mimeType)) {
            final String extension =
                mimeType != null && mimeType.toLowerCase().contains("csv")
                    ? ".csv"
                    : ".txt";
            displayName = "landstar_" +
                          new SimpleDateFormat("yyyyMMdd_HHmmss")
                              .format(new Date().getTime()) +
                          extension;
        }
        final long declaredLength =
            documentFile != null ? documentFile.length() : -1L;
        if (!isLandStarPointFile(displayName) ||
            declaredLength > 25L * 1024L * 1024L) {
            displayAlertDialog(
                "LandStar 측점 연결 실패",
                "TXT, CSV, PXY 또는 KOF 측점 파일(최대 25 MB)을 선택해 주세요.");
            return;
        }

        final File destination = uniqueLandStarInboxFile(displayName);
        if (destination == null) {
            displayAlertDialog("LandStar 측점 연결 실패",
                               "앱의 LandStar 수신 폴더를 만들지 못했습니다.");
            return;
        }
        final ProgressDialog wait = new ProgressDialog(this);
        wait.setMessage("LandStar 측점을 확인하는 중입니다…");
        wait.setIndeterminate(true);
        wait.setCancelable(false);
        wait.show();

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                boolean imported = false;
                try (InputStream input = resolver.openInputStream(uri)) {
                    imported = copyLandStarPointFile(
                        input, destination, declaredLength);
                } catch (Exception error) {
                    Log.e("SungsanLandStar",
                          "Could not copy LandStar point file.", error);
                }
                final boolean succeeded = imported;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        wait.dismiss();
                        if (succeeded) {
                            landStarFileReceived(destination.getAbsolutePath());
                        } else {
                            destination.delete();
                            new File(destination.getAbsolutePath() + ".source")
                                .delete();
                            displayAlertDialog(
                                "LandStar 측점 연결 실패",
                                "선택한 측점 파일을 앱으로 안전하게 복사하지 못했습니다.");
                        }
                    }
                });
            }
        });
    }

    private void dimBrightness() {
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        originalBrightness = lp.screenBrightness;
        lp.screenBrightness = 0.01f;
        getWindow().setAttributes(lp);
    }

    private void restoreBrightness() {
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = originalBrightness;
        getWindow().setAttributes(lp);
    }

    private void takeVolumeKeys() {
        handleVolumeKeys = true;
    }

    private void releaseVolumeKeys() {
        handleVolumeKeys = false;
    }

    private void showBlockingProgressDialog(String message) {
        progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(message);
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();
    }

    private void dismissBlockingProgressDialog() {
        if (progressDialog != null) {
            progressDialog.dismiss();
            progressDialog = null;
        }
    }

    private void displayAlertDialog(String title, String message) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                AlertDialog alertDialog =
                    new AlertDialog.Builder(QFieldActivity.this).create();
                alertDialog.setTitle(title);
                alertDialog.setMessage(message);
                alertDialog.show();
            }
        });
    }

    private void initiateSentry() {
        Context context = getApplication().getApplicationContext();

        try {
            ApplicationInfo app =
                context.getPackageManager().getApplicationInfo(
                    context.getPackageName(), PackageManager.GET_META_DATA);
            Bundle bundle = app.metaData;
            SentryAndroid.init(this, options -> {
                options.setDsn(bundle.getString("io.sentry.dsn"));
                options.setEnvironment(
                    bundle.getString("io.sentry.environment"));
            });
        } catch (NameNotFoundException e) {
            return;
        }
    }

    private void prepareQtActivity() {
        sharedPreferences =
            getSharedPreferences("QField", Context.MODE_PRIVATE);
        sharedPreferenceEditor = sharedPreferences.edit();

        checkAllFileAccess(); // Storage access permission handling for Android
                              // 11+

        List<String> dataDirs = new ArrayList<String>();

        File primaryExternalFilesDir = getApplicationDir();

        if (primaryExternalFilesDir != null) {
            String dataDir = primaryExternalFilesDir.getAbsolutePath() + "/";
            // create import and creation directories
            new File(dataDir + "Imported Datasets/").mkdir();
            new File(dataDir + "Imported Projects/").mkdir();
            new File(dataDir + "Created Projects/").mkdir();

            dataDir = dataDir + "QField/";
            // create QField directories
            new File(dataDir).mkdir();
            new File(dataDir + "basemaps/").mkdir();
            new File(dataDir + "fonts/").mkdir();
            new File(dataDir + "proj/").mkdir();
            new File(dataDir + "auth/").mkdir();
            new File(dataDir + "logs/").mkdirs();
            new File(dataDir + "plugins/").mkdirs();

            dataDirs.add(dataDir);
        }

        String storagePath =
            Environment.getExternalStorageDirectory().getAbsolutePath();
        String rootDataDir = storagePath + "/QField/";
        File storageFile = new File(rootDataDir);
        storageFile.mkdir();
        if (storageFile.canWrite()) {
            // create directories
            new File(rootDataDir + "basemaps/").mkdir();
            new File(rootDataDir + "fonts/").mkdir();
            new File(rootDataDir + "proj/").mkdir();
            new File(rootDataDir + "auth/").mkdir();
            new File(rootDataDir + "logs/").mkdirs();

            dataDirs.add(rootDataDir);
        }

        File[] externalFilesDirs = getExternalFilesDirs(null);
        for (File file : externalFilesDirs) {
            if (file != null) {
                // Don't duplicate primary external files directory
                if (primaryExternalFilesDir != null &&
                    file.getAbsolutePath().equals(
                        primaryExternalFilesDir.getAbsolutePath())) {
                    continue;
                }

                // create QField directories
                String dataDir = file.getAbsolutePath() + "/QField/";
                new File(dataDir + "basemaps/").mkdirs();
                new File(dataDir + "fonts/").mkdirs();
                new File(dataDir + "proj/").mkdirs();
                new File(dataDir + "auth/").mkdirs();
                new File(dataDir + "logs/").mkdirs();

                dataDirs.add(dataDir);
            }
        }

        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setClass(QFieldActivity.this, QtActivity.class);
        // Prevent activity restart
        intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP |
                        Intent.FLAG_ACTIVITY_SINGLE_TOP);
        try {
            ActivityInfo activityInfo = getPackageManager().getActivityInfo(
                getComponentName(), PackageManager.GET_META_DATA);
            intent.putExtra("GIT_REV", activityInfo.metaData.getString(
                                           "android.app.git_rev"));
        } catch (NameNotFoundException e) {
            e.printStackTrace();
            finish();
            return;
        }

        StringBuilder appDataDirs = new StringBuilder();
        for (String dataDir : dataDirs) {
            appDataDirs.append(dataDir);
            appDataDirs.append("--;--");
        }
        intent.putExtra("QFIELD_APP_DATA_DIRS", appDataDirs.toString());

        Intent sourceIntent = getIntent();
        if (Intent.ACTION_VIEW.equals(sourceIntent.getAction()) ||
            Intent.ACTION_SEND.equals(sourceIntent.getAction())) {
            String scheme = sourceIntent.getScheme();
            if (scheme != null && scheme.equals("qfield")) {
                qfieldIntent = sourceIntent;
                intent.putExtra("QF_ACTION", "trigger_load");
            } else if (scheme != null && scheme.equals("https")) {
                Uri uri = sourceIntent.getData();
                String host = uri.getHost();
                if (host.equals("qfield.org")) {
                    qfieldIntent = sourceIntent;
                    intent.putExtra("QF_ACTION", "trigger_load");
                }
            } else {
                projectIntent = sourceIntent;
                intent.putExtra("QGS_PROJECT", "trigger_load");
            }
        }

        setIntent(intent);
    }

    private String getApplicationDirectory() {
        File primaryExternalFilesDir = getApplicationDir();

        if (primaryExternalFilesDir != null) {
            return primaryExternalFilesDir.getAbsolutePath();
        }

        return "";
    }

    private String getAdditionalApplicationDirectories() {
        List<String> dirs = new ArrayList<String>();

        File externalStorageDirectory = null;
        if (ContextCompat.checkSelfPermission(
                QFieldActivity.this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
                PackageManager.PERMISSION_GRANTED ||
            (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
             Environment.isExternalStorageManager())) {
            externalStorageDirectory =
                Environment.getExternalStorageDirectory();
        }

        File primaryExternalFilesDir = getApplicationDir();
        File[] externalFilesDirs = getExternalFilesDirs(null);
        for (File file : externalFilesDirs) {
            if (file != null) {
                // Don't duplicate external files directory or storage
                // path already added
                if (primaryExternalFilesDir != null &&
                    file.getAbsolutePath().equals(
                        primaryExternalFilesDir.getAbsolutePath())) {
                    continue;
                }
                if (externalStorageDirectory != null) {
                    if (!file.getAbsolutePath().contains(
                            externalStorageDirectory.getAbsolutePath())) {
                        dirs.add(file.getAbsolutePath());
                    }
                } else {
                    dirs.add(file.getAbsolutePath());
                }
            }
        }

        StringBuilder rootDirs = new StringBuilder();
        for (String dir : dirs) {
            rootDirs.append(dir);
            rootDirs.append("--;--");
        }
        return rootDirs.toString();
    }

    private String getRootDirectories() {
        List<String> dirs = new ArrayList<String>();

        File externalStorageDirectory = null;
        if (ContextCompat.checkSelfPermission(
                QFieldActivity.this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
                PackageManager.PERMISSION_GRANTED ||
            (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
             Environment.isExternalStorageManager())) {
            externalStorageDirectory =
                Environment.getExternalStorageDirectory();
            if (externalStorageDirectory != null) {
                dirs.add(externalStorageDirectory.getAbsolutePath());
            }
        }

        StringBuilder rootDirs = new StringBuilder();
        for (String dir : dirs) {
            rootDirs.append(dir);
            rootDirs.append("--;--");
        }
        return rootDirs.toString();
    }

    private void triggerImportDatasets() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, IMPORT_DATASET);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                getString(R.string.import_operation_unsupported));
            Log.w("QField", "No activity found for ACTION_OPEN_DOCUMENT.");
        }
        return;
    }

    private void triggerImportLandStarPoints() {
        if (requiresSungsanActivation() && !hasValidSungsanActivation()) {
            showSungsanActivationDialog();
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(Intent.EXTRA_MIME_TYPES,
                        new String[] {"text/plain", "text/csv",
                                      "application/csv",
                                      "application/octet-stream"});
        intent.setType("*/*");
        try {
            startActivityForResult(intent, IMPORT_LANDSTAR_POINTS);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                "LandStar 측점 파일을 선택할 수 있는 파일 앱이 없습니다.");
            Log.w("Sungsan", "No activity found for LandStar file selection.");
        }
    }

    private void triggerImportProjectFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, IMPORT_PROJECT_FOLDER);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                getString(R.string.import_operation_unsupported));
            Log.w("QField", "No activity found for ACTION_OPEN_DOCUMENT_TREE.");
        }
        return;
    }

    private void triggerImportProjectArchive() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.setType("application/zip");
        try {
            startActivityForResult(intent, IMPORT_PROJECT_ARCHIVE);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                getString(R.string.import_operation_unsupported));
            Log.w("QField", "No activity found for ACTION_OPEN_DOCUMENT.");
        }
        return;
    }

    private void triggerUpdateProjectFromArchive(String path) {
        projectPath = path;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.setType("application/zip");
        try {
            startActivityForResult(intent, UPDATE_PROJECT_FROM_ARCHIVE);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                getString(R.string.import_operation_unsupported));
            Log.w("QField", "No activity found for ACTION_OPEN_DOCUMENT.");
        }
        return;
    }

    private void sendDatasetTo(String paths) {
        showBlockingProgressDialog(getString(R.string.processing_message));

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                String[] filePaths = paths.split("--;--");
                File file;
                if (filePaths.length == 1) {
                    file = new File(paths);
                } else {
                    File temporaryFile = new File(filePaths[0]);
                    file = new File(getCacheDir(),
                                    temporaryFile.getName() + ".zip");
                    try {
                        OutputStream out =
                            new FileOutputStream(file.getAbsolutePath());
                        boolean success =
                            QFieldUtils.filesToZip(out, filePaths);
                        out.close();
                        if (!success) {
                            return;
                        }
                    } catch (Exception e) {
                        dismissBlockingProgressDialog();
                        e.printStackTrace();
                        return;
                    }
                }
                DocumentFile documentFile = DocumentFile.fromFile(file);
                dismissBlockingProgressDialog();

                Context context = getApplication().getApplicationContext();
                Intent intent = new Intent(Intent.ACTION_SEND);
                intent.putExtra(Intent.EXTRA_STREAM,
                                FileProvider.getUriForFile(
                                    context,
                                    context.getPackageName() + ".fileprovider",
                                    file));
                intent.setType(documentFile.getType());
                startActivity(Intent.createChooser(intent, null));
                return;
            }
        });
        return;
    }

    private void exportToFolder(String paths) {
        pathsToExport = paths;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addCategory(Intent.CATEGORY_DEFAULT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, EXPORT_TO_FOLDER);
        } catch (ActivityNotFoundException e) {
            displayAlertDialog(
                getString(R.string.operation_unsupported),
                getString(R.string.export_operation_unsupported));
            Log.w("QField", "No activity found for ACTION_OPEN_DOCUMENT_TREE.");
        }
        return;
    }

    private void removeDataset(String path) {
        File file = new File(path);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle(getString(R.string.delete_confirm_title));
        builder.setMessage(getString(R.string.delete_confirm_dataset));
        builder.setPositiveButton(
            getString(R.string.delete_confirm),
            new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int id) {
                    file.delete();
                    dialog.dismiss();
                    openPath(file.getParentFile().getAbsolutePath());
                }
            });
        builder.setNegativeButton(getString(R.string.delete_cancel),
                                  new DialogInterface.OnClickListener() {
                                      public void onClick(
                                          DialogInterface dialog, int id) {
                                          dialog.dismiss();
                                      }
                                  });
        AlertDialog dialog = builder.create();
        dialog.setCancelable(false);
        dialog.show();
        return;
    }

    private void sendCompressedFolderTo(String path) {
        showBlockingProgressDialog(getString(R.string.processing_message));

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                final File file = new File(path);
                final File temporaryFile =
                    new File(getCacheDir(), file.getName() + ".zip");
                boolean archiveReady = false;

                try {
                    // Never allow a previous successful export to mask a new
                    // compression failure.
                    if (temporaryFile.exists() && !temporaryFile.delete()) {
                        Log.w("QField", "Could not remove stale project ZIP.");
                    }

                    archiveReady =
                        file.isDirectory() &&
                        QFieldUtils.folderToZip(file.getPath(),
                                                temporaryFile.getPath()) &&
                        isNonEmptyReadableZip(temporaryFile);
                } catch (Exception e) {
                    Log.e("QField", "Could not create project ZIP.", e);
                    archiveReady = false;
                }

                if (!archiveReady && temporaryFile.exists() &&
                    !temporaryFile.delete()) {
                    Log.w("QField", "Could not remove incomplete project ZIP.");
                }

                final boolean canShareArchive = archiveReady;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        dismissBlockingProgressDialog();

                        if (!canShareArchive) {
                            if (!isFinishing()) {
                                displayAlertDialog(
                                    getString(R.string.export_error),
                                    getString(
                                        R.string.export_project_zip_error));
                            }
                            return;
                        }

                        if (isFinishing()) {
                            return;
                        }

                        try {
                            Context context = QFieldActivity.this;
                            Intent intent = new Intent(Intent.ACTION_SEND);
                            intent.putExtra(
                                Intent.EXTRA_STREAM,
                                FileProvider.getUriForFile(
                                    context,
                                    context.getPackageName() + ".fileprovider",
                                    temporaryFile));
                            intent.addFlags(
                                Intent.FLAG_GRANT_READ_URI_PERMISSION);
                            intent.setType("application/zip");
                            startActivity(Intent.createChooser(intent, null));
                        } catch (Exception e) {
                            Log.e("QField", "Could not share project ZIP.", e);
                            if (!temporaryFile.delete()) {
                                Log.w("QField",
                                      "Could not remove unshared project ZIP.");
                            }
                            if (!isFinishing()) {
                                displayAlertDialog(
                                    getString(R.string.export_error),
                                    getString(
                                        R.string.export_project_zip_error));
                            }
                        }
                    }
                });
            }
        });
        return;
    }

    private boolean isNonEmptyReadableZip(File archive) {
        if (!archive.isFile() || archive.length() == 0) {
            return false;
        }

        try (ZipFile zipFile = new ZipFile(archive)) {
            int projectFileCount = 0;
            int regularFileCount = 0;
            byte[] buffer = new byte[8192];
            Enumeration<? extends ZipEntry> entries = zipFile.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry.isDirectory()) {
                    continue;
                }

                regularFileCount++;
                String entryName = entry.getName().toLowerCase();
                if ((entryName.endsWith(".qgs") ||
                     entryName.endsWith(".qgz")) &&
                    !entryName.contains(".qfieldsync")) {
                    if (entry.getSize() <= 0) {
                        return false;
                    }
                    projectFileCount++;
                }

                CRC32 crc = new CRC32();
                long uncompressedBytes = 0;
                try (InputStream input = zipFile.getInputStream(entry)) {
                    int size;
                    while ((size = input.read(buffer)) != -1) {
                        if (size == 0) {
                            continue;
                        }
                        crc.update(buffer, 0, size);
                        uncompressedBytes += size;
                    }
                }
                if ((entry.getSize() >= 0 &&
                     uncompressedBytes != entry.getSize()) ||
                    (entry.getCrc() >= 0 && crc.getValue() != entry.getCrc())) {
                    Log.e(
                        "QField", "Project ZIP entry validation failed: " +
                                      entry.getName());
                    return false;
                }
            }
            return regularFileCount > 0 && projectFileCount == 1;
        } catch (IOException e) {
            Log.e("QField", "Project ZIP validation failed.", e);
            return false;
        }
    }

    private File createSiblingStagingDirectory(File target, String label)
        throws IOException {
        File parent = target.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            throw new IOException("Project import parent is unavailable.");
        }

        for (int attempt = 0; attempt < 100; attempt++) {
            File staging = new File(
                parent,
                "." + target.getName() + "." + label + "-" +
                    System.nanoTime() + "-" + attempt);
            if (staging.mkdir()) {
                return staging;
            }
        }
        throw new IOException("Could not create a project staging folder.");
    }

    private boolean copyDirectoryTree(File source, File destination) {
        if (!source.isDirectory() ||
            (!destination.isDirectory() && !destination.mkdirs())) {
            return false;
        }

        File[] children = source.listFiles();
        if (children == null) {
            return false;
        }
        for (File child : children) {
            File destinationChild = new File(destination, child.getName());
            boolean copied = child.isDirectory()
                                 ? copyDirectoryTree(child, destinationChild)
                                 : QFieldUtils.copyFile(child,
                                                        destinationChild);
            if (!copied) {
                return false;
            }
        }
        return true;
    }

    private boolean deletePath(File path) {
        try {
            if (!path.exists()) {
                return true;
            }
            return path.isDirectory()
                       ? QFieldUtils.deleteDirectory(path, true)
                       : path.delete();
        } catch (Exception e) {
            Log.e("QField", "Could not clean up project import path.", e);
            return false;
        }
    }

    private boolean replaceDirectoryFromStaging(File staging, File target) {
        File backup = null;
        if (target.exists()) {
            try {
                backup = createSiblingStagingDirectory(target, "backup");
            } catch (IOException e) {
                Log.e("QField", "Could not reserve project backup path.", e);
                return false;
            }
            if (!backup.delete() || !target.renameTo(backup)) {
                Log.e("QField", "Could not move existing project to backup.");
                return false;
            }
        }

        if (staging.renameTo(target)) {
            if (backup != null && !deletePath(backup)) {
                Log.w("QField", "Could not remove replaced project backup.");
            }
            return true;
        }

        Log.e("QField", "Could not commit staged project import.");
        if (backup != null && !backup.renameTo(target)) {
            Log.e("QField", "Could not restore existing project backup.");
        }
        return false;
    }

    private boolean installNewDirectoryFromStaging(File staging, File target) {
        if (staging == null || !staging.isDirectory() || target.exists()) {
            Log.e("QField", "Refusing to overwrite an imported project folder.");
            return false;
        }
        return staging.renameTo(target);
    }

    private String projectFolderNameForArchive(DocumentFile documentFile) {
        String archiveName =
            documentFile == null ? null : documentFile.getName();
        if (archiveName == null) {
            return "";
        }
        archiveName = archiveName.replace('\\', '/');
        archiveName = archiveName.substring(archiveName.lastIndexOf('/') + 1);
        int extensionIndex = archiveName.lastIndexOf('.');
        String folderName = extensionIndex > 0
                                ? archiveName.substring(0, extensionIndex)
                                : archiveName;
        return folderName.equals(".") || folderName.equals("..")
                   ? ""
                   : folderName;
    }

    private void removeProjectFolder(String path) {
        File file = new File(path);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle(getString(R.string.delete_confirm_title));
        builder.setMessage(getString(R.string.delete_confirm_folder));
        builder.setPositiveButton(
            getString(R.string.delete_confirm),
            new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int id) {
                    QFieldUtils.deleteDirectory(file, true);
                    dialog.dismiss();
                    openPath(file.getParentFile().getAbsolutePath());
                }
            });
        builder.setNegativeButton(getString(R.string.delete_cancel),
                                  new DialogInterface.OnClickListener() {
                                      public void onClick(
                                          DialogInterface dialog, int id) {
                                          dialog.dismiss();
                                      }
                                  });
        AlertDialog dialog = builder.create();
        dialog.setCancelable(false);
        dialog.show();
        return;
    }

    private void getCameraResource(String prefix, String filePath,
                                   String suffix, boolean isVideo) {
        resourcePrefix = prefix;
        resourceFilePath = filePath;
        resourceSuffix = suffix;

        String timeStamp =
            new SimpleDateFormat("yyyyMMdd_HHmmss").format(new Date());
        resourceTempFilePath = "QFieldCamera" + timeStamp;

        Intent intent = isVideo ? new Intent(MediaStore.ACTION_VIDEO_CAPTURE)
                                : new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
        if (intent.resolveActivity(getPackageManager()) != null) {
            Log.d("QField", "Camera intent resolved");
            File storageDir =
                getExternalFilesDir(Environment.DIRECTORY_PICTURES);
            try {
                File tempFile = File.createTempFile(resourceTempFilePath,
                                                    suffix, storageDir);

                if (tempFile != null) {
                    Log.d("QField", "Temporary camera file created");
                    if (tempFile.exists()) {
                        Log.d(
                            "QField",
                            "Temporary camera file exists already, it will be overwritten");
                    }

                    resourceTempFilePath = tempFile.getAbsolutePath();

                    Uri fileURI = FileProvider.getUriForFile(
                        this, getPackageName() + ".fileprovider", tempFile);

                    Log.d("QField",
                          "Camera temporary file uri: " + fileURI.toString());
                    intent.putExtra(MediaStore.EXTRA_OUTPUT, fileURI);
                    Log.d("QField", "Camera intent starting");
                    startActivityForResult(intent, CAMERA_RESOURCE);
                }
            } catch (IOException e) {
                Log.d("QField", e.getMessage());
                resourceCanceled("");
            }
        } else {
            Log.d("QField", "Could not resolve camera intent");
            resourceCanceled("");
        }
        return;
    }

    private void getGalleryResource(String prefix, String filePath,
                                    String mimeType) {
        resourcePrefix = prefix;
        resourceFilePath = filePath;

        Intent intent;
        if (Build.VERSION.SDK_INT >= 33) {
            intent = new Intent(MediaStore.ACTION_PICK_IMAGES);
            intent.setType(mimeType);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } else {
            intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.setType(mimeType);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        }
        Log.d("QField", "Gallery intent starting");
        startActivityForResult(intent, GALLERY_RESOURCE);
        return;
    }

    private void getFilePickerResource(String prefix, String filePath,
                                       String mimeType) {
        resourcePrefix = prefix;
        resourceFilePath = filePath;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);
        intent.setType(mimeType);
        Log.d("QField", "File picker intent starting");
        startActivityForResult(intent, FILE_PICKER_RESOURCE);
        return;
    }

    private void openResource(String filePath, String mimeType,
                              boolean isEditing) {
        resourceFilePath = filePath;
        resourceIsEditing = isEditing;

        resourceFile = new File(filePath);
        resourceCacheFile = new File(getCacheDir(), resourceFile.getName());

        // Copy resource to a temporary file
        if (QFieldUtils.copyFile(resourceFile, resourceCacheFile)) {
            Uri contentUri = Build.VERSION.SDK_INT < 24
                                 ? Uri.fromFile(resourceFile)
                                 : FileProvider.getUriForFile(
                                       this, getPackageName() + ".fileprovider",
                                       resourceCacheFile);

            Intent intent =
                new Intent(isEditing ? Intent.ACTION_EDIT : Intent.ACTION_VIEW);
            if (isEditing) {
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                if (mimeType.contains("image/")) {
                    intent.setDataAndType(contentUri, "image/*");
                } else {
                    intent.setDataAndType(contentUri, mimeType);
                }
                intent.putExtra(MediaStore.EXTRA_OUTPUT, contentUri);
            } else {
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                intent.setDataAndType(contentUri, mimeType);
            }
            try {
                Log.d("QField", "Open intent starting");
                startActivityForResult(intent, OPEN_RESOURCE);
            } catch (IllegalArgumentException e) {
                Log.d("QField", e.getMessage());
                resourceCanceled("");
            } catch (Exception e) {
                Log.d("QField", e.getMessage());
                resourceCanceled("");
            }
        } else {
            resourceCanceled("");
        }

        return;
    }

    void importDatasets(Uri[] datasetUris) {
        File externalFilesDir = getApplicationDir();

        if (externalFilesDir == null || datasetUris.length == 0) {
            return;
        }

        ProgressDialog progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(getString(R.string.import_dataset_wait));
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();

        String importDatasetPath =
            externalFilesDir.getAbsolutePath() + "/Imported Datasets/";
        new File(importDatasetPath).mkdir();

        Context context = getApplication().getApplicationContext();
        ContentResolver resolver = getContentResolver();

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                boolean imported = false;
                for (Uri datasetUri : datasetUris) {
                    DocumentFile documentFile =
                        DocumentFile.fromSingleUri(context, datasetUri);
                    String importFilePath =
                        importDatasetPath + documentFile.getName();
                    try {
                        InputStream input =
                            resolver.openInputStream(datasetUri);
                        imported = QFieldUtils.inputStreamToFile(
                            input, importFilePath, documentFile.length());
                    } catch (Exception e) {
                        e.printStackTrace();
                        imported = false;
                    }
                    if (!imported) {
                        break;
                    }
                }

                progressDialog.dismiss();
                if (!imported) {
                    if (!isFinishing()) {
                        displayAlertDialog(
                            getString(R.string.import_error),
                            getString(R.string.import_dataset_error));
                    }
                } else {
                    openPath(importDatasetPath);
                }
            }
        });
    }

    void importProjectFolder(Uri folderUri) {
        File externalFilesDir = getApplicationDir();

        if (externalFilesDir == null) {
            return;
        }

        ProgressDialog progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(getString(R.string.import_project_wait));
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();

        String importProjectPath =
            externalFilesDir.getAbsolutePath() + "/Imported Projects/";
        new File(importProjectPath).mkdir();

        Context context = getApplication().getApplicationContext();
        ContentResolver resolver = getContentResolver();

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                DocumentFile directory =
                    DocumentFile.fromTreeUri(context, folderUri);
                String importPath =
                    importProjectPath + directory.getName() + "/";
                new File(importPath).mkdir();
                boolean imported = QFieldUtils.documentFileToFolder(
                    directory, importPath, resolver);

                progressDialog.dismiss();
                if (imported) {
                    openPath(importPath);
                } else {
                    if (!isFinishing()) {
                        displayAlertDialog(
                            getString(R.string.import_error),
                            getString(R.string.import_project_folder_error));
                    }
                }
            }
        });
    }

    void importProjectArchive(Uri archiveUri) {
        File externalFilesDir = getApplicationDir();

        if (externalFilesDir == null) {
            return;
        }

        ProgressDialog progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(getString(R.string.import_project_wait));
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();

        String importProjectPath =
            externalFilesDir.getAbsolutePath() + "/Imported Projects/";
        new File(importProjectPath).mkdir();

        Context context = getApplication().getApplicationContext();
        ContentResolver resolver = getContentResolver();

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                DocumentFile documentFile =
                    DocumentFile.fromSingleUri(context, archiveUri);
                String projectName = "";
                File targetDirectory = null;
                File stagingDirectory = null;
                boolean imported = false;

                try {
                    String projectFolderName =
                        projectFolderNameForArchive(documentFile);
                    if (!projectFolderName.isEmpty()) {
                        File importRoot = new File(importProjectPath);
                        targetDirectory =
                            new File(importRoot, projectFolderName);
                        int suffix = 1;
                        while (targetDirectory.exists()) {
                            targetDirectory = new File(
                                importRoot,
                                projectFolderName + "_" + suffix);
                            suffix++;
                        }
                        stagingDirectory = createSiblingStagingDirectory(
                            targetDirectory, "importing");

                        try (InputStream input =
                                 resolver.openInputStream(archiveUri)) {
                            projectName =
                                QFieldUtils.getArchiveProjectName(input);
                        }

                        if (!projectName.isEmpty()) {
                            try (InputStream input =
                                     resolver.openInputStream(archiveUri)) {
                                imported = input != null &&
                                           QFieldUtils.zipToFolder(
                                               input,
                                               stagingDirectory
                                                       .getAbsolutePath() +
                                                   File.separator);
                            }
                            imported =
                                imported &&
                                new File(stagingDirectory, projectName).isFile();
                            if (imported) {
                                imported = installNewDirectoryFromStaging(
                                    stagingDirectory, targetDirectory);
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.e("QField", "Could not import project archive.", e);
                    imported = false;
                } finally {
                    if (stagingDirectory != null &&
                        stagingDirectory.exists() &&
                        !deletePath(stagingDirectory)) {
                        Log.w("QField",
                              "Could not remove project import staging data.");
                    }
                }

                final boolean importSucceeded = imported;
                final String importedProjectPath =
                    targetDirectory == null || projectName.isEmpty()
                        ? ""
                        : new File(targetDirectory, projectName)
                              .getAbsolutePath();
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        progressDialog.dismiss();
                        if (importSucceeded) {
                            openProject(importedProjectPath);
                        } else if (!isFinishing()) {
                            displayAlertDialog(
                                getString(R.string.import_error),
                                getString(
                                    R.string.import_project_archive_error));
                        }
                    }
                });
            }
        });
    }

    void updateProjectFromArchive(Uri archiveUri) {
        File externalFilesDir = getApplicationDir();
        if (externalFilesDir == null) {
            return;
        }

        ProgressDialog progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(getString(R.string.update_project_wait));
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();

        ContentResolver resolver = getContentResolver();

        executorService.execute(new Runnable() {
            @Override
            public void run() {
                final String originalProjectPath = projectPath;
                File currentProject = new File(originalProjectPath);
                File projectFolder = currentProject.getParentFile();
                File stagingDirectory = null;
                boolean imported = false;
                String archivedProjectName = "";

                try {
                    if (!clearProject()) {
                        throw new IOException(
                            "Could not close the current project safely.");
                    }
                    if (projectFolder != null && projectFolder.isDirectory()) {
                        stagingDirectory = createSiblingStagingDirectory(
                            projectFolder, "updating");
                        imported = copyDirectoryTree(projectFolder,
                                                     stagingDirectory);

                        if (imported) {
                            try (InputStream input =
                                     resolver.openInputStream(archiveUri)) {
                                archivedProjectName =
                                    QFieldUtils.getArchiveProjectName(input);
                            }
                            imported = !archivedProjectName.isEmpty();
                        }
                        if (imported) {
                            try (InputStream input =
                                     resolver.openInputStream(archiveUri)) {
                                imported = input != null &&
                                           QFieldUtils.zipToFolder(
                                               input,
                                               stagingDirectory
                                                       .getAbsolutePath() +
                                                   File.separator);
                            }
                            imported =
                                imported &&
                                new File(stagingDirectory,
                                         archivedProjectName).isFile();
                            if (imported) {
                                File stagedOriginalProject = new File(
                                    stagingDirectory,
                                    currentProject.getName());
                                File stagedArchivedProject = new File(
                                    stagingDirectory,
                                    archivedProjectName);
                                if (!stagedOriginalProject.getCanonicalFile()
                                         .equals(stagedArchivedProject
                                                     .getCanonicalFile()) &&
                                    stagedOriginalProject.exists() &&
                                    !stagedOriginalProject.delete()) {
                                    imported = false;
                                }
                            }
                        }
                        if (imported) {
                            imported = replaceDirectoryFromStaging(
                                stagingDirectory, projectFolder);
                        }
                    }
                } catch (Exception e) {
                    Log.e("QField", "Could not update project archive.", e);
                    imported = false;
                } finally {
                    if (stagingDirectory != null &&
                        stagingDirectory.exists() &&
                        !deletePath(stagingDirectory)) {
                        Log.w("QField",
                              "Could not remove project update staging data.");
                    }
                }

                final boolean updateSucceeded = imported;
                final String reopenedProjectPath =
                    updateSucceeded && projectFolder != null &&
                            !archivedProjectName.isEmpty()
                        ? new File(projectFolder, archivedProjectName)
                              .getAbsolutePath()
                        : originalProjectPath;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        progressDialog.dismiss();
                        // A successful archive may intentionally rename its
                        // QGS/QGZ. On failure the transaction leaves the old
                        // folder intact, so the original path is reopened.
                        openProject(reopenedProjectPath);
                        if (!updateSucceeded && !isFinishing()) {
                            displayAlertDialog(
                                getString(R.string.import_error),
                                getString(
                                    R.string.import_project_archive_error));
                        }
                    }
                });
            }
        });
    }

    private void checkStoragePermissions() {
        List<String> permissionsList = new ArrayList<String>();
        if (ContextCompat.checkSelfPermission(
                QFieldActivity.this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
            PackageManager.PERMISSION_DENIED) {
            permissionsList.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(
                    QFieldActivity.this,
                    Manifest.permission.READ_MEDIA_IMAGES) ==
                PackageManager.PERMISSION_DENIED) {
                permissionsList.add(Manifest.permission.READ_MEDIA_IMAGES);
            }
            if (ContextCompat.checkSelfPermission(
                    QFieldActivity.this,
                    Manifest.permission.READ_MEDIA_VIDEO) ==
                PackageManager.PERMISSION_DENIED) {
                permissionsList.add(Manifest.permission.READ_MEDIA_VIDEO);
            }
        }
        if (ContextCompat.checkSelfPermission(
                QFieldActivity.this,
                Manifest.permission.ACCESS_MEDIA_LOCATION) ==
            PackageManager.PERMISSION_DENIED) {
            permissionsList.add(Manifest.permission.ACCESS_MEDIA_LOCATION);
        }
        if (permissionsList.size() > 0) {
            String[] permissions = new String[permissionsList.size()];
            permissionsList.toArray(permissions);
            ActivityCompat.requestPermissions(QFieldActivity.this, permissions,
                                              101);
        }
    }

    private void checkAllFileAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
            !Environment.isExternalStorageManager() &&
            !sharedPreferences.getBoolean("DontAskAllFilesPermission", false)) {
            // if MANAGE_EXTERNAL_STORAGE permission isn't in the manifest,
            // bail out
            String[] requestedPermissions;
            try {
                PackageInfo pi = getPackageManager().getPackageInfo(
                    this.getPackageName(), PackageManager.GET_PERMISSIONS);
                requestedPermissions = pi.requestedPermissions;
            } catch (NameNotFoundException e) {
                e.printStackTrace();
                finish();
                return;
            }
            if (!Arrays.asList(requestedPermissions)
                     .contains(Manifest.permission.MANAGE_EXTERNAL_STORAGE)) {
                return;
            }

            checkStoragePermissions();

            AlertDialog.Builder builder = new AlertDialog.Builder(this);
            builder.setTitle(getString(R.string.grant_permission));
            builder.setMessage(
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.N
                    ? Html.fromHtml(
                          getString(R.string.grant_all_files_permission),
                          Html.FROM_HTML_MODE_LEGACY)
                    : Html.fromHtml(
                          getString(R.string.grant_all_files_permission)));
            builder.setPositiveButton(
                getString(R.string.grant),
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        try {
                            Uri uri = Uri.parse("package:" + getPackageName());
                            Intent intent = new Intent(
                                Settings
                                    .ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                uri);
                            startActivity(intent);
                        } catch (Exception e) {
                            Log.e(
                                "QField",
                                "Failed to initial activity to grant all files access",
                                e);
                        }
                        dialog.dismiss();
                    }
                });
            builder.setNegativeButton(
                getString(R.string.deny_always),
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        sharedPreferenceEditor.putBoolean(
                            "DontAskAllFilesPermission", true);
                        sharedPreferenceEditor.commit();

                        dialog.dismiss();
                    }
                });

            builder.setNeutralButton(getString(R.string.deny_once),
                                     new DialogInterface.OnClickListener() {
                                         public void onClick(
                                             DialogInterface dialog, int id) {
                                             dialog.dismiss();
                                         }
                                     });

            AlertDialog dialog = builder.create();
            dialog.setCancelable(false);
            dialog.show();
        }
    }

    private File getApplicationDir() {
        File applicationDirectory = getExternalFilesDir(null);
        if (applicationDirectory == null) {
            // On some Android devices, getExternalFilesDir(null) can return a
            // null value, fallback to getFilesDir()
            applicationDirectory = getFilesDir();
        }
        return applicationDirectory;
    }

    protected void onActivityResult(int requestCode, int resultCode,
                                     Intent data) {
        if (requestCode == IMPORT_LANDSTAR_POINTS) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                importLandStarUri(data.getData());
            }
            return;
        } else if (requestCode == CAMERA_RESOURCE) {
            if (resultCode == RESULT_OK && resourceTempFilePath != null) {
                File file = new File(resourceTempFilePath);
                String finalFilePath = QFieldUtils.replaceFilenameTags(
                    resourceFilePath, file.getName());
                File result = new File(resourcePrefix + finalFilePath);
                Log.d("QField",
                      "Taken camera picture: " + file.getAbsolutePath());
                try {
                    InputStream in = new FileInputStream(file);
                    QFieldUtils.inputStreamToFile(in, result.getPath(),
                                                  file.length());
                    file.delete();
                } catch (Exception e) {
                    e.printStackTrace();
                }

                // Let the android scan new media folders/files to make them
                // visible through MTP
                result.setReadable(true);
                result.setWritable(true);
                MediaScannerConnection.scanFile(
                    this, new String[] {result.getParentFile().toString()},
                    null, null);
                resourceReceived(finalFilePath);
            } else {
                resourceCanceled("");
            }
        } else if (requestCode == GALLERY_RESOURCE && data != null) {
            if (resultCode == RESULT_OK) {
                Uri uri = data.getData();
                DocumentFile documentFile = DocumentFile.fromSingleUri(
                    getApplication().getApplicationContext(), uri);
                String finalFilePath = QFieldUtils.replaceFilenameTags(
                    resourceFilePath, documentFile.getName());
                File result = new File(resourcePrefix + finalFilePath);
                Log.d("QField",
                      "Selected gallery file: " + data.getData().toString());
                try {
                    InputStream in = getContentResolver().openInputStream(uri);
                    QFieldUtils.inputStreamToFile(in, result.getPath(),
                                                  documentFile.length());
                } catch (Exception e) {
                    Log.d("QField", e.getMessage());
                }

                // Let the android scan new media folders/files to make them
                // visible through MTP
                result.setReadable(true);
                result.setWritable(true);
                MediaScannerConnection.scanFile(
                    this, new String[] {result.getParentFile().toString()},
                    null, null);
                resourceReceived(finalFilePath);
            } else {
                resourceCanceled("");
            }
        } else if (requestCode == FILE_PICKER_RESOURCE) {
            if (resultCode == RESULT_OK && data != null) {
                Uri uri = data.getData();
                DocumentFile documentFile = DocumentFile.fromSingleUri(
                    getApplication().getApplicationContext(), uri);
                String finalFilePath = QFieldUtils.replaceFilenameTags(
                    resourceFilePath, documentFile.getName());
                File result = new File(resourcePrefix + finalFilePath);
                Log.d("QField", "Selected file picker file: " +
                                    data.getData().toString());
                try {
                    InputStream in = getContentResolver().openInputStream(uri);
                    QFieldUtils.inputStreamToFile(in, result.getPath(),
                                                  documentFile.length());
                } catch (Exception e) {
                    Log.d("QField", e.getMessage());
                }

                resourceReceived(finalFilePath);
            } else {
                resourceCanceled("");
            }
        } else if (requestCode == OPEN_RESOURCE) {
            if (resultCode == RESULT_OK && data != null) {
                try {
                    if (resourceIsEditing) {
                        Log.d(
                            "QField",
                            "Copy file back from uri " + data.getDataString() +
                                " to file: " + resourceFile.getAbsolutePath());
                        InputStream in = getContentResolver().openInputStream(
                            data.getData());
                        OutputStream out = new FileOutputStream(resourceFile);
                        // Transfer bytes from in to out
                        byte[] buf = new byte[1024];
                        int len;
                        while ((len = in.read(buf)) > 0) {
                            out.write(buf, 0, len);
                        }
                        out.close();
                    }
                    resourceOpened(resourceFile.getAbsolutePath());
                } catch (SecurityException e) {
                    resourceCanceled(e.getMessage());
                } catch (IOException e) {
                    resourceCanceled(e.getMessage());
                }
            } else {
                resourceCanceled("");
            }
        } else if (requestCode == IMPORT_DATASET &&
                   resultCode == Activity.RESULT_OK) {
            Log.d("QField", "handling import dataset(s)");
            File externalFilesDir = getApplicationDir();
            if (externalFilesDir == null || data == null) {
                return;
            }

            String importDatasetPath =
                externalFilesDir.getAbsolutePath() + "/Imported Datasets/";

            Context context = getApplication().getApplicationContext();
            ContentResolver resolver = getContentResolver();

            Uri[] datasetUris;
            if (data.getClipData() != null) {
                datasetUris = new Uri[data.getClipData().getItemCount()];
                for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                    datasetUris[i] = data.getClipData().getItemAt(i).getUri();
                }
            } else {
                datasetUris = new Uri[1];
                datasetUris[0] = data.getData();
            }

            boolean hasExists = false;
            for (Uri datasetUri : datasetUris) {
                DocumentFile documentFile =
                    DocumentFile.fromSingleUri(context, datasetUri);
                File importFilePath =
                    new File(importDatasetPath + documentFile.getName());
                if (importFilePath.exists()) {
                    hasExists = true;
                    break;
                }
            }

            if (hasExists) {
                AlertDialog.Builder builder = new AlertDialog.Builder(this);
                builder.setTitle(getString(R.string.import_overwrite_title));
                builder.setMessage(
                    datasetUris.length > 1
                        ? getString(R.string.import_overwrite_dataset_multiple)
                        : getString(R.string.import_overwrite_dataset_single));
                builder.setPositiveButton(
                    getString(R.string.import_overwrite_confirm),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            importDatasets(datasetUris);
                            dialog.dismiss();
                        }
                    });
                builder.setNegativeButton(
                    getString(R.string.import_overwrite_cancel),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            dialog.dismiss();
                        }
                    });
                AlertDialog dialog = builder.create();
                dialog.setCancelable(false);
                dialog.show();
            } else {
                importDatasets(datasetUris);
            }
        } else if (requestCode == IMPORT_PROJECT_FOLDER &&
                   resultCode == Activity.RESULT_OK) {
            Log.d("QField", "handling import project folder");
            File externalFilesDir = getApplicationDir();
            if (externalFilesDir == null || data == null) {
                return;
            }

            Uri uri = data.getData();
            Context context = getApplication().getApplicationContext();
            DocumentFile directory = DocumentFile.fromTreeUri(context, uri);
            File importPath =
                new File(externalFilesDir.getAbsolutePath() +
                         "/Imported Projects/" + directory.getName() + "/");
            if (importPath.exists()) {
                AlertDialog.Builder builder = new AlertDialog.Builder(this);
                builder.setTitle(getString(R.string.import_overwrite_title));
                builder.setMessage(getString(R.string.import_overwrite_folder));
                builder.setPositiveButton(
                    getString(R.string.import_overwrite_confirm),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            importProjectFolder(uri);
                            dialog.dismiss();
                        }
                    });
                builder.setNegativeButton(
                    getString(R.string.import_overwrite_cancel),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            dialog.dismiss();
                        }
                    });
                AlertDialog dialog = builder.create();
                dialog.setCancelable(false);
                dialog.show();
            } else {
                importProjectFolder(uri);
            }
        } else if (requestCode == IMPORT_PROJECT_ARCHIVE &&
                   resultCode == Activity.RESULT_OK) {
            Log.d("QField", "handling import project archive");
            File externalFilesDir = getApplicationDir();
            if (externalFilesDir == null || data == null) {
                return;
            }

            String importProjectPath =
                externalFilesDir.getAbsolutePath() + "/Imported Projects/";
            new File(importProjectPath).mkdir();

            Uri uri = data.getData();
            if (uri == null) {
                displayAlertDialog(
                    getString(R.string.import_error),
                    getString(R.string.import_project_archive_error));
                return;
            }
            Context context = getApplication().getApplicationContext();

            DocumentFile documentFile =
                DocumentFile.fromSingleUri(context, uri);

            String projectFolderName =
                projectFolderNameForArchive(documentFile);
            if (projectFolderName.isEmpty()) {
                displayAlertDialog(
                    getString(R.string.import_error),
                    getString(R.string.import_project_archive_error));
                return;
            }
            importProjectPath =
                importProjectPath + "/" + projectFolderName + "/";

            File importPath = new File(importProjectPath);
            if (importPath.exists()) {
                AlertDialog.Builder builder = new AlertDialog.Builder(this);
                builder.setTitle(getString(R.string.import_overwrite_title));
                builder.setMessage(getString(R.string.import_overwrite_folder));
                builder.setPositiveButton(
                    getString(R.string.import_overwrite_confirm),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            importProjectArchive(uri);
                            dialog.dismiss();
                        }
                    });
                builder.setNegativeButton(
                    getString(R.string.import_overwrite_cancel),
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int id) {
                            dialog.dismiss();
                        }
                    });
                AlertDialog dialog = builder.create();
                dialog.setCancelable(false);
                dialog.show();
            } else {
                importProjectArchive(uri);
            }
        } else if (requestCode == UPDATE_PROJECT_FROM_ARCHIVE &&
                   resultCode == Activity.RESULT_OK) {
            Log.d("QField", "handling updating project from archive");
            File externalFilesDir = getApplicationDir();
            if (externalFilesDir == null || data == null) {
                return;
            }

            Uri uri = data.getData();
            Context context = getApplication().getApplicationContext();
            ContentResolver resolver = getContentResolver();

            DocumentFile documentFile =
                DocumentFile.fromSingleUri(context, uri);

            updateProjectFromArchive(uri);
        } else if (requestCode == EXPORT_TO_FOLDER &&
                   resultCode == Activity.RESULT_OK && pathsToExport != null) {
            Log.d("QField", "handling export to folder");

            String[] paths = pathsToExport.split("--;--");
            Uri uri = data.getData();
            Context context = getApplication().getApplicationContext();
            ContentResolver resolver = getContentResolver();

            executorService.execute(new Runnable() {
                @Override
                public void run() {
                    resolver.takePersistableUriPermission(
                        uri, Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                 Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                    DocumentFile directory =
                        DocumentFile.fromTreeUri(context, uri);

                    boolean exported = true;
                    for (String path : paths) {
                        File file = new File(path);
                        exported = QFieldUtils.fileToDocumentFile(
                            file, directory, resolver);
                        if (!exported) {
                            break;
                        }
                    }

                    if (!exported) {
                        if (!isFinishing()) {
                            displayAlertDialog(
                                getString(R.string.export_error),
                                getString(R.string.export_to_folder_error));
                        }
                    }
                }
            });
        } else {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }
}
