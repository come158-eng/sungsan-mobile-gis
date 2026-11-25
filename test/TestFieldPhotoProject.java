// SPDX-License-Identifier: GPL-2.0-or-later
package ch.opengis.qfield;

import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import java.io.*;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.*;

public class TestFieldPhotoProject {
    private static String settings(String id) {
        StringBuilder spec = new StringBuilder("<photoExport><layer id='" + id + "'>");
        for (int i=1; i<=4; ++i) spec.append("<field name='p"+i+"' index='"+i+"' alias='slot "+i+"'><Option type='Map'><Option type='int' name='RelativeStorage' value='1'/></Option></field>");
        return spec.append("</layer></photoExport>").toString();
    }

    static void require(boolean value) {
        if (!value) throw new AssertionError();
    }

    public static void main(String[] args) throws Exception {
        Path temp = Paths.get(args[0]);
        String original = "<?xml version='1.0'?><!DOCTYPE qgis SYSTEM 'http://example.invalid/qgis.dtd'><qgis version='3.40'>"
          +"<projectlayers><maplayer><id>other</id><renderer-v2 type='custom'/></maplayer><maplayer><id>active</id>"
          +"<fieldConfiguration><field name='p1'/><field name='p2'/><field name='p3'/></fieldConfiguration>"
          +"<aliases><alias field='p1' index='1' name='custom caption'/></aliases><editorlayout>tablayout</editorlayout>"
          +"<attributeEditorForm><attributeEditorContainer name='existing group'><attributeEditorField name='p1'/><attributeEditorRelation relation='child'/></attributeEditorContainer></attributeEditorForm></maplayer></projectlayers></qgis>";
        Path source=temp.resolve("한국어.qgs"),output=temp.resolve("output.qgs");
        byte[] xml=original.getBytes(StandardCharsets.UTF_8);
        Files.write(source,xml);
        FieldPhotoProject.writeCopy(source.toFile(), settings("active"), output.toFile());
        require(Arrays.equals(Files.readAllBytes(source),xml));
        DocumentBuilderFactory db=DocumentBuilderFactory.newInstance();
        javax.xml.parsers.DocumentBuilder reader=db.newDocumentBuilder();
        reader.setEntityResolver((x,y)->new org.xml.sax.InputSource(new StringReader("")));
        Document doc=reader.parse(output.toFile());
        require(doc.getElementsByTagName("field").getLength()==4);
        require(doc.getElementsByTagName("renderer-v2").getLength()==1 && doc.getElementsByTagName("attributeEditorRelation").getLength()==1);
        NodeList shown=doc.getElementsByTagName("attributeEditorField");
        require(shown.getLength()==2 && ((Element)shown.item(1)).getAttribute("name").equals("p4") && shown.item(0).getParentNode()==shown.item(1).getParentNode());
        require(((Element)doc.getElementsByTagName("alias").item(0)).getAttribute("name").equals("custom caption"));
        Path qgz=temp.resolve("other-name.qgz"),returned=temp.resolve("returned.qgz");
        byte[] attachment={0,3,8,127,-1};
        try(ZipOutputStream out=new ZipOutputStream(Files.newOutputStream(qgz))) {
            out.putNextEntry(new ZipEntry("original.qgs"));out.write(xml);out.closeEntry();
            out.putNextEntry(new ZipEntry("original.qgd"));out.write(attachment);out.closeEntry();
        }
        byte[] initial=Files.readAllBytes(qgz);
        FieldPhotoProject.writeCopy(qgz.toFile(),settings("active"),returned.toFile());
        require(Arrays.equals(initial,Files.readAllBytes(qgz)));
        try(ZipFile z=new ZipFile(returned.toFile())){
            require(Arrays.equals(attachment,z.getInputStream(z.getEntry("original.qgd")).readAllBytes()));
            require(Arrays.equals(Files.readAllBytes(output),z.getInputStream(z.getEntry("original.qgs")).readAllBytes()));
        }
        boolean rejected=false;
        try {FieldPhotoProject.writeCopy(qgz.toFile(),settings("not-present"),returned.toFile());}
        catch(IOException ex){rejected=true;}
        require(rejected);
        System.out.println("PASS QGS and QGZ photo fields, selected layer ID, hidden fields, custom layout/aliases, original project and attachment bytes, missing-layer rejection.");
    }
}
