/***************************************************************************
                            QFieldUtils.java
                            -------------------
              begin                : December 6, 2020
              copyright            : (C) 2020 by Mathieu Pellerin
              email                : nirvn dot asia at gmail dot com
 ***************************************************************************/

// Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11: hardened project
// ZIP extraction against path traversal and excessive archive expansion.

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

package ch.opengis.qfield;

import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.util.Log;
import android.webkit.MimeTypeMap;
import androidx.documentfile.provider.DocumentFile;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.SecurityException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

public class QFieldUtils {

    private static final int MAX_PROJECT_ARCHIVE_ENTRIES = 100000;
    private static final long MAX_PROJECT_ARCHIVE_UNCOMPRESSED_BYTES =
        8L * 1024L * 1024L * 1024L;

    /**
     * Returns a modified string with attachment filename tags replaced with
     * their values.
     * @param  string   the string to be modified
     * @param  filename the filename from which tags values are derived from
     * @return          modified String
     */
    public static String replaceFilenameTags(String string, String filename) {
        String extension = "";
        int dotIndex = filename.indexOf('.');
        if (dotIndex > -1) {
            extension = filename.substring(dotIndex + 1);
        }

        if (string != null) {
            string = string.replace("{filename}", filename)
                         .replace("{extension}", extension);
        } else {
            string = filename;
        }

        return string;
    }

    /**
     * Returns the only project name found in a given ZIP archive. If no
     * project file is found, or if the archive is ambiguous and contains more
     * than one QGS/QGZ, the returned string will be empty.
     * @param  in a ZIP archive's InputStream
     * @return    the only project name found in the archive
     */
    public static String getArchiveProjectName(InputStream in) {
        String projectName = "";
        try (ZipInputStream zin = new ZipInputStream(in)) {
            ZipEntry entry;
            while ((entry = zin.getNextEntry()) != null) {
                String entryName = entry.getName().toLowerCase();
                if ((entryName.endsWith(".qgs") ||
                     entryName.endsWith(".qgz")) &&
                    !entryName.contains(".qfieldsync")) {
                    if (!projectName.isEmpty()) {
                        Log.e("QField",
                              "Project ZIP contains multiple QGS/QGZ files.");
                        return "";
                    }
                    projectName = entry.getName();
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
            return "";
        }
        return projectName;
    }

    /**
     * Recursively copies a DocumentFile directory into a specified directory.
     * @param directory the DocumentFile directory from which files and
     *     sub-directories will be copied
     * @param folder    the destination directory in which files will be copied
     * @param resolver  the content resolver used to open files
     * @return          returns True if the operation was successful
     */
    public static boolean documentFileToFolder(DocumentFile directory,
                                               String folder,
                                               ContentResolver resolver) {
        DocumentFile[] files = directory.listFiles();
        for (DocumentFile file : files) {
            if (file.isDirectory()) {
                String directoryPath = folder + file.getName() + "/";
                new File(directoryPath).mkdir();
                boolean success =
                    documentFileToFolder(file, directoryPath, resolver);
                if (!success) {
                    return false;
                }
            } else {
                String filePath = folder + file.getName();
                try {
                    InputStream input = resolver.openInputStream(file.getUri());
                    QFieldUtils.inputStreamToFile(input, filePath,
                                                  file.length());
                } catch (Exception e) {
                    e.printStackTrace();
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * Copies file(s) into a specified DocumentFile directory.
     * @param file      the file or directory to be copied (if a directory is
     *     provided, its content will be copied recursively)
     * @param directory the destination DocumentFile directory in which the file
     *     will be copied into
     * @param resolver  the content resolver used to open the directory
     * @return          returns True if the operation was successful
     */
    public static boolean fileToDocumentFile(File file, DocumentFile directory,
                                             ContentResolver resolver) {
        if (file == null || directory == null) {
            return false;
        }

        File[] files =
            file.isDirectory() ? file.listFiles() : new File[] {file};
        for (File f : files) {
            String filePath = f.getPath();
            String fileName = f.getName();
            if (f.isDirectory()) {
                // Use pre-existing directory if present
                DocumentFile newDirectory = directory.findFile(fileName);
                if (newDirectory == null) {
                    newDirectory = directory.createDirectory(fileName);
                }
                boolean success = fileToDocumentFile(f, newDirectory, resolver);
                if (!success) {
                    return false;
                }
            } else {
                String extension = "";
                String mimeType = "";
                if (fileName.lastIndexOf(".") > -1) {
                    extension =
                        fileName.substring(fileName.lastIndexOf(".") + 1);
                    mimeType =
                        MimeTypeMap.getSingleton().getMimeTypeFromExtension(
                            extension);
                }
                // Use pre-existing file if present
                DocumentFile documentFile = directory.findFile(fileName);
                if (documentFile == null) {
                    documentFile = directory.createFile(mimeType, fileName);
                }
                if (documentFile == null) {
                    return false;
                }
                try (InputStream input = new FileInputStream(f);
                     OutputStream output =
                         resolver.openOutputStream(documentFile.getUri())) {
                    if (output == null ||
                        !QFieldUtils.inputStreamToOutputStream(
                            input, output, f.length())) {
                        return false;
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * Creates a ZIP archive from the provided list of files.
     * @param out   the OutputStream used to create the ZIP archive
     * @param files an array of file paths to be added into the ZIP archive
     * @return      returns True if the operation was successful
     */
    public static boolean filesToZip(OutputStream out, String[] files) {
        try {
            ZipOutputStream zout = new ZipOutputStream(out);
            for (String path : files) {
                File file = new File(path);
                InputStream in = new FileInputStream(file);
                ZipEntry entry = new ZipEntry(file.getName());
                zout.putNextEntry(entry);
                int size = 0;
                byte[] buffer = new byte[1024];
                while ((size = in.read(buffer, 0, buffer.length)) != -1) {
                    zout.write(buffer, 0, size);
                }
                in.close();
            }
            zout.close();
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
        return true;
    }

    /**
     * Extracts the content of a ZIP archive into the provided directory.
     * @param in     the ZIP archive's InputStream
     * @param folder the directory in which the content of the ZIP archive will
     *     be copied
     * @return       returns True if the operation was successful
     */
    public static boolean zipToFolder(InputStream in, String folder) {
        if (in == null || folder == null) {
            return false;
        }

        try (ZipInputStream zin = new ZipInputStream(in)) {
            File destinationRoot = new File(folder).getCanonicalFile();
            if (!destinationRoot.isDirectory() && !destinationRoot.mkdirs()) {
                return false;
            }
            String destinationPrefix =
                destinationRoot.getPath() + File.separator;
            int entryCount = 0;
            long uncompressedBytes = 0;
            ZipEntry entry;
            while ((entry = zin.getNextEntry()) != null) {
                entryCount++;
                if (entryCount > MAX_PROJECT_ARCHIVE_ENTRIES) {
                    throw new IOException(
                        "Project ZIP contains too many entries.");
                }

                String entryName = entry.getName();
                if (entryName == null || entryName.isEmpty()) {
                    throw new SecurityException(
                        "Project ZIP contains an invalid entry path.");
                }
                File destination =
                    new File(destinationRoot, entryName).getCanonicalFile();
                if (!destination.getPath().startsWith(destinationPrefix)) {
                    throw new SecurityException(
                        "ZIP path traversal attack detected, aborting.");
                }

                if (entry.isDirectory()) {
                    if (!destination.isDirectory() &&
                        !destination.mkdirs()) {
                        throw new IOException(
                            "Could not create project ZIP directory.");
                    }
                    continue;
                }

                // Some ZIP files omit directory entries, so create the parent
                // only after validating the canonical destination path.
                File parent = destination.getParentFile();
                if (parent == null ||
                    (!parent.isDirectory() && !parent.mkdirs())) {
                    throw new IOException(
                        "Could not create project ZIP parent directory.");
                }

                try (OutputStream out = new FileOutputStream(destination)) {
                    int size;
                    byte[] buffer = new byte[8192];
                    while ((size = zin.read(buffer, 0, buffer.length)) != -1) {
                        uncompressedBytes += size;
                        if (uncompressedBytes >
                            MAX_PROJECT_ARCHIVE_UNCOMPRESSED_BYTES) {
                            throw new IOException(
                                "Project ZIP expands beyond the safety limit.");
                        }
                        out.write(buffer, 0, size);
                    }
                }
            }
        } catch (Exception e) {
            Log.e("QField", "zipToFolder exception: " + e.getMessage());
            return false;
        }
        return true;
    }

    /**
     * Creates a ZIP archive from the content of a provided directory.
     * @param folder      the folder path within which the content will
     *     recursively be compressed into the ZIP archive
     * @param archivePath the file path for the created ZIP archive
     * @return            returns True if the operation was successful
     */
    public static boolean folderToZip(String folder, String archivePath) {
        try (ZipOutputStream zip = new ZipOutputStream(
                 new FileOutputStream(archivePath))) {
            File sourceRoot = new File(folder).getCanonicalFile();
            if (!sourceRoot.isDirectory() ||
                !addFolderToZip(zip, sourceRoot.getPath(),
                                sourceRoot.getPath())) {
                return false;
            }
            zip.finish();
            return true;
        } catch (Exception e) {
            Log.e("QField", "folderToZip exception: " + e.getMessage());
        }
        return false;
    }

    /**
     * Adds the content of a given folder into the ZIP archive being created.
     * @param zip        the ZIP archive's ZipOutputStream
     * @param folder     the folder path within which the content will
     *     recursively be compressed into the ZIP archive
     * @param rootFolder the root folder path when initiating the ZIP archive
     *     creation
     * @return           returns True if the operation was successful
     */
    private static boolean addFolderToZip(ZipOutputStream zip, String folder,
                                          String rootFolder) {
        File dir;
        File root;
        try {
            dir = new File(folder).getCanonicalFile();
            root = new File(rootFolder).getCanonicalFile();
            String rootPrefix = root.getPath() + File.separator;
            if (!dir.equals(root) &&
                !dir.getPath().startsWith(rootPrefix)) {
                Log.e("QField", "Project folder escaped its ZIP root.");
                return false;
            }
        } catch (IOException e) {
            Log.e("QField", "Could not resolve project folder: " + folder, e);
            return false;
        }

        File[] files = dir.listFiles();
        if (files == null) {
            Log.e("QField", "Could not list project folder: " + folder);
            return false;
        }
        String pathPrefix = "";
        if (folder.length() > rootFolder.length()) {
            pathPrefix = folder.substring(rootFolder.length() + 1);
            if (!pathPrefix.substring(pathPrefix.length() - 1).equals("/")) {
                pathPrefix = pathPrefix + "/";
            }
        }
        for (File file : files) {
            try {
                File canonicalFile = file.getCanonicalFile();
                String rootPrefix = root.getPath() + File.separator;
                if (!canonicalFile.getPath().startsWith(rootPrefix) ||
                    !canonicalFile.equals(file.getAbsoluteFile())) {
                    Log.e("QField",
                          "Refusing a symbolic link or escaped project file: " +
                              file.getPath());
                    return false;
                }
            } catch (IOException e) {
                Log.e("QField", "Could not resolve project file: " +
                                        file.getPath(), e);
                return false;
            }

            String fileName = file.getName();

            if (file.isDirectory()) {
                boolean success =
                    addFolderToZip(zip, file.getPath(), rootFolder);
                if (!success) {
                    return false;
                }
            } else {
                try {
                    ZipEntry zipFile = new ZipEntry(pathPrefix + fileName);
                    zip.putNextEntry(zipFile);
                    try (InputStream input = new FileInputStream(file)) {
                        if (!inputStreamToOutputStream(input, zip,
                                                       file.length())) {
                            return false;
                        }
                    } finally {
                        zip.closeEntry();
                    }
                } catch (Exception e) {
                    Log.e("QField", "Could not add project file to ZIP: " +
                                        e.getMessage());
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * Copies an InputStream to an OutputStream.
     * @param in         the InputStream used to copy content
     * @param out        the OutputStream used to copy the InputStream content
     *     into
     * @param totalBytes the total bytes of content to be copied from the
     *     InputStream
     * @return           returns True if the operation was successful
     */
    public static boolean inputStreamToOutputStream(InputStream in,
                                                    OutputStream out,
                                                    long totalBytes) {
        if (in == null || out == null) {
            return false;
        }

        try {
            int size;
            long bytesRead = 0;
            byte[] buffer = new byte[8192];
            while ((size = in.read(buffer, 0, buffer.length)) != -1) {
                if (size == 0) {
                    continue;
                }
                out.write(buffer, 0, size);
                bytesRead += size;
            }
            out.flush();
            return totalBytes <= 0 || bytesRead == totalBytes;
        } catch (Exception e) {
            Log.e("QField",
                  "inputStreamToOutputStream exception: " + e.getMessage());
            return false;
        }
    }

    /**
     * Copies an InputStream to a provided file path.
     * @param in         the InputStream used to copy content
     * @param file       the file path used to copy the InputStream content into
     * @param totalBytes the total bytes of content to be copied from the
     *     InputStream
     * @return           returns True if the operation was successful
     */
    public static boolean inputStreamToFile(InputStream in, String file,
                                            long totalBytes) {
        try (OutputStream out = new FileOutputStream(new File(file))) {
            return inputStreamToOutputStream(in, out, totalBytes);
        } catch (Exception e) {
            Log.e("QField", "inputStreamToFile exception: " + e.getMessage());
            return false;
        }
    }

    public static boolean copyFile(File src, File dst) {
        try (InputStream in = new FileInputStream(src);
             OutputStream out = new FileOutputStream(dst)) {
            // Transfer bytes from in to out
            return inputStreamToOutputStream(in, out, src.length());
        } catch (Exception e) {
            Log.e("QField", "copyFile exception: " + e.getMessage());
            return false;
        }
    }

    /**
     * Deletes the provided directory.
     * @param file      the directory to be deleted
     * @param recursive if set to True, sub-directories will be deleted
     * @return           returns True if the operation was successful
     */
    public static boolean deleteDirectory(File file, boolean recursive) {
        if (!file.isDirectory()) {
            return false;
        }
        File[] files = file.listFiles();
        for (File f : files) {
            boolean success = true;
            if (f.isDirectory()) {
                if (recursive) {
                    success = deleteDirectory(f, true);
                }
            } else {
                success = f.delete();
            }
            if (!success) {
                return false;
            }
        }
        return file.delete();
    }

    /**
     * Returns the extension string for a given mime type. If no match
     * available, an empty string will be returned.
     * @param type a mime type string
     * @return     returns the extension string
     */
    public static String getExtensionFromMimeType(String type) {
        if (type == null) {
            return "";
        } else if (type.equals("application/pdf")) {
            return "pdf";
        } else if (type.equals("application/vnd.sqlite3") ||
                   type.equals("application/x-sqlite3")) {
            return "db";
        } else if (type.equals("application/geopackage+sqlite3")) {
            return "gpkg";
        } else if (type.equals("application/vnd.geo+json") ||
                   type.equals("application/geo+json")) {
            return "geojson";
        } else if (type.equals("application/gpx+xml")) {
            return "gpx";
        } else if (type.equals("application/vnd.google-earth.kml+xml")) {
            return "kml";
        } else if (type.equals("application/vnd.google-earth.kmz")) {
            return "kmz";
        } else if (type.equals("application/zip")) {
            return "zip";
        } else if (type.equals("image/tiff")) {
            return "tif";
        } else if (type.equals("image/x-jp2")) {
            return "jp2";
        }
        return "";
    }

    // original script by SANJAY GUPTA
    // (https://stackoverflow.com/questions/17546101/get-real-path-for-uri-android)
    public static String getPathFromUri(final Context context, final Uri uri) {
        String path = null;

        if (DocumentsContract.isDocumentUri(context, uri)) {
            // DocumentProvider
            if (isExternalStorageDocument(uri)) {
                // ExternalStorageProvider
                final String docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];

                if ("primary".equalsIgnoreCase(type)) {
                    return Environment.getExternalStorageDirectory() + "/" +
                        split[1];
                }
                // TODO handle non-primary volumes
            } else if (isDownloadsDocument(uri)) {
                // DownloadsProvider
                try {
                    final String id = DocumentsContract.getDocumentId(uri);
                    final Uri contentUri = ContentUris.withAppendedId(
                        Uri.parse("content://downloads/public_downloads"),
                        Long.valueOf(id));

                    path = getDataColumn(context, contentUri, null, null);
                } catch (NumberFormatException e) {
                    // Not numerical IDs, skipping
                    path = null;
                }
            } else if (isMediaDocument(uri)) {
                // MediaProvider
                final String docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];

                Uri contentUri = null;
                if ("image".equals(type)) {
                    contentUri = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
                } else if ("video".equals(type)) {
                    contentUri = MediaStore.Video.Media.EXTERNAL_CONTENT_URI;
                } else if ("audio".equals(type)) {
                    contentUri = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
                }

                final String selection = "_id=?";
                final String[] selectionArgs = new String[] {split[1]};

                path = getDataColumn(context, contentUri, selection,
                                     selectionArgs);
            }
        }

        // Fallback
        if (uri == null) {
            return path;
        }

        if (path == null && ("content".equalsIgnoreCase(uri.getScheme()) ||
                             "file".equalsIgnoreCase(uri.getScheme()))) {
            path = uri.getPath();
            if (path != null) {
                path = path.replaceFirst("^/storage_root", "");
            }
        }

        if (path != null) {
            if (new File(path).exists() == false) {
                path = "";
            }
        }

        return path;
    }

    public static String getDataColumn(Context context, Uri uri,
                                       String selection,
                                       String[] selectionArgs) {

        Cursor cursor = null;
        final String column = "_data";
        final String[] projection = {column};

        try {
            cursor = context.getContentResolver().query(
                uri, projection, selection, selectionArgs, null);
            if (cursor != null && cursor.moveToFirst()) {
                final int index = cursor.getColumnIndexOrThrow(column);
                return cursor.getString(index);
            }
        } catch (Exception e) {
            if (cursor != null)
                cursor.close();
        } finally {
            if (cursor != null)
                cursor.close();
        }
        return null;
    }

    public static boolean isExternalStorageDocument(Uri uri) {
        return "com.android.externalstorage.documents".equals(
            uri.getAuthority());
    }

    public static boolean isDownloadsDocument(Uri uri) {
        return "com.android.providers.downloads.documents".equals(
            uri.getAuthority());
    }

    public static boolean isMediaDocument(Uri uri) {
        return "com.android.providers.media.documents".equals(
            uri.getAuthority());
    }
}
