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

    private static void legacyDisplayName(Path temp) throws Exception {
        String old = "성산_현장객체", ns = "kr.co.sungsan.mobilegis";
        String uri = "./data/field_data.gpkg|layername=" + old;
        StringBuilder properties = new StringBuilder(), fields = new StringBuilder();
        for (String name : Arrays.asList("fieldObjects","fieldPackage","landstarImportTarget"))
            properties.append("<Option name='"+ns+"/"+name+"' type='bool' value='true'/>");
        for (String name : Arrays.asList("object_id","name","memo","surveyed_at","photo_near","photo_far","photo_other"))
            fields.append("<field name='"+name+"'/>");
        String layer = "<maplayer type='vector' geometry='Point'><id>managed</id><layername>"+old+"</layername>"
                +"<provider>ogr</provider><datasource>"+uri+"</datasource><fieldConfiguration>"+fields+"</fieldConfiguration>"
                +"<customproperties><Option type='Map'>"+properties+"</Option></customproperties></maplayer>";
        String user = layer.replace("<id>managed","<id>user").replace(ns+"/fieldObjects","user/fieldObjects");
        String wrongTable = layer.replace("<id>managed","<id>table").replace(uri, "./data/user.gpkg|layername="+old);
        String renamed = layer.replace("<id>managed","<id>renamed").replace("<layername>"+old,"<layername>업무용");
        String tree="<layer-tree-group><layer-tree-group><layer-tree-layer id='managed' name='"+old+"'/>"
                +"<layer-tree-layer id='user' name='"+old+"'/><layer-tree-layer id='managed' name='범례 별칭'/></layer-tree-group></layer-tree-group>";
        String xml="<qgis version='3.40'>"+tree+"<properties><SungsanMobileGIS><fieldPackage>"+ns+".field-package/1"
                +"</fieldPackage></SungsanMobileGIS></properties><projectlayers>"+layer+user+wrongTable+renamed
                +"</projectlayers><action name='"+old+"'/></qgis>";
        Path input=temp.resolve("legacy-display.qgs"),output=temp.resolve("returned-display.qgs");
        for (boolean duplicate : new boolean[]{false,true}) {
            String data=duplicate?xml.replace("</projectlayers>",layer.replace("<id>managed","<id>duplicate")+"</projectlayers>"):xml;
            Files.writeString(input,data, StandardCharsets.UTF_8);
            FieldPhotoProject.writeCopy(input.toFile(),settings("managed"),output.toFile());
            Document doc=DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(output.toFile());
            NodeList names=doc.getElementsByTagName("layername"), nodes=doc.getElementsByTagName("layer-tree-layer");
            String expected=duplicate?old:"현장객체";
            require(expected.equals(names.item(0).getTextContent()) && old.equals(names.item(1).getTextContent())
                    && old.equals(names.item(2).getTextContent()) && "업무용".equals(names.item(3).getTextContent()));
            require(expected.equals(((Element)nodes.item(0)).getAttribute("name")) && old.equals(((Element)nodes.item(1)).getAttribute("name"))
                    && "범례 별칭".equals(((Element)nodes.item(2)).getAttribute("name")));
            require(uri.equals(doc.getElementsByTagName("datasource").item(0).getTextContent())
                    && old.equals(((Element)doc.getElementsByTagName("action").item(0)).getAttribute("name")));
        }
    }

    public static void main(String[] args) throws Exception {
        Path temp = Paths.get(args[0]);
        legacyDisplayName(temp);
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
