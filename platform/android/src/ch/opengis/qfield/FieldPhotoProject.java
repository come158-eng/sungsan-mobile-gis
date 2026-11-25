// SPDX-License-Identifier: GPL-2.0-or-later
package ch.opengis.qfield;

import java.io.*;
import java.util.*;
import java.util.zip.*;
import java.util.regex.Pattern;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.transform.*;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;
import org.w3c.dom.*;
import org.xml.sax.InputSource;

/** Make a portable return copy without reserializing the desktop project through mobile QGIS. */
final class FieldPhotoProject {
    private FieldPhotoProject() {}
    private static final Set<String> LEGACY_OBJECT_NAMES = new HashSet<>(Arrays.asList(
            "성산_현장객체", "성산 현장 객체", "성산 현장객체", "메타_현장객체", "메타이엔지_현장객체"));

    static void writeCopy(File source, String settings, File output) throws Exception {
        if (source.getCanonicalFile().equals(output.getCanonicalFile())) {
            throw new IOException("The project copy must be distinct from its source");
        }
        Document spec = parse(new InputSource(new StringReader(settings)));
        if (!"photoExport".equals(spec.getDocumentElement().getTagName())) {
            throw new IOException("Invalid field-photo configuration");
        }
        if (source.getName().toLowerCase(Locale.ROOT).endsWith(".qgs")) {
            try (InputStream in = new BufferedInputStream(new FileInputStream(source));
                 OutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
                patch(in, out, spec, source.getCanonicalFile().getParentFile());
            }
            return;
        }
        if (!source.getName().toLowerCase(Locale.ROOT).endsWith(".qgz")) {
            throw new IOException("Unsupported desktop project");
        }
        try (ZipFile archive = new ZipFile(source);
             ZipOutputStream out = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(output)))) {
            List<? extends ZipEntry> entries = Collections.list(archive.entries());
            ZipEntry main = projectEntry(entries);
            byte[] buffer = new byte[65536];
            for (ZipEntry entry : entries) {
                ZipEntry copied = new ZipEntry(entry.getName());
                copied.setTime(entry.getTime());
                copied.setComment(entry.getComment());
                out.putNextEntry(copied);
                CRC32 crc = new CRC32();
                try (CheckedInputStream in = new CheckedInputStream(archive.getInputStream(entry), crc)) {
                    if (entry == main) {
                        patch(in, out, spec, source.getCanonicalFile().getParentFile());
                    } else {
                        int count;
                        while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                    }
                    if (crc.getValue() != entry.getCrc()) throw new IOException("Invalid project member CRC");
                }
                out.closeEntry();
            }
        }
    }

    private static Document parse(InputSource source) throws Exception {
        DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
        factory.setNamespaceAware(true);
        factory.setExpandEntityReferences(false);
        DocumentBuilder reader = factory.newDocumentBuilder();
        // QGIS uses a public doctype; only the local XML is needed.
        reader.setEntityResolver((id, system) -> new InputSource(new StringReader("")));
        return reader.parse(source);
    }

    private static List<Element> children(Element parent, String tag) {
        List<Element> elements = new ArrayList<>();
        if (parent != null) {
            for (Node n = parent.getFirstChild(); n != null; n = n.getNextSibling()) {
                if (n.getNodeType() == Node.ELEMENT_NODE && tag.equals(n.getNodeName()))
                    elements.add((Element)n);
            }
        }
        return elements;
    }

    private static Element child(Element parent, String tag) {
        for (Element element : children(parent, tag)) return element;
        return null;
    }

    private static String text(Element parent, String tag) {
        Element item = child(parent, tag);
        return item == null ? "" : item.getTextContent();
    }

    private static boolean isManagedObject(Element qgis, Element layer) {
        if (!LEGACY_OBJECT_NAMES.contains(text(layer,"layername")) || !"vector".equals(layer.getAttribute("type"))
                || !"Point".equals(layer.getAttribute("geometry")) || !"ogr".equals(text(layer,"provider"))) return false;
        String source = text(layer,"datasource");
        if (source.startsWith("./")) source = source.substring(2);
        String prefix = "data/field_data.gpkg|layername=";
        boolean packageTable = source.startsWith(prefix) && (LEGACY_OBJECT_NAMES.contains(source.substring(prefix.length()))
                || "현장객체".equals(source.substring(prefix.length())));
        if (!packageTable && !"sungsan_field.gpkg|layername=sungsan_field_objects".equals(source)
                && !"metaengi_field.gpkg|layername=metaengi_field_objects".equals(source)) return false;
        Set<String> names = new HashSet<>();
        for (Element field : children(child(layer,"fieldConfiguration"),"field")) names.add(field.getAttribute("name"));
        if (!names.containsAll(Arrays.asList("object_id","name","memo","surveyed_at","photo_near","photo_far","photo_other"))) return false;
        Set<String> markers = new HashSet<>();
        for (Element option : children(child(child(layer,"customproperties"),"Option"),"Option")) {
            if ("bool".equals(option.getAttribute("type")) && "true".equals(option.getAttribute("value")))
                markers.add(option.getAttribute("name"));
        }
        Element properties = child(qgis,"properties");
        for (String[] brand : new String[][] {{"SungsanMobileGIS","kr.co.sungsan.mobilegis"}, {"MetaEngiMobileGIS","kr.co.metaengi.mobilegis"}}) {
            String ns = brand[1];
            if ((ns+".field-package/1").equals(text(child(properties,brand[0]),"fieldPackage"))
                    && markers.contains(ns+"/fieldObjects") && markers.contains(ns+"/fieldPackage")
                    && markers.contains(ns+"/landstarImportTarget")) return true;
        }
        return false;
    }

    private static void normalizeLegacyObjectName(Element qgis) {
        Element object = null;
        for (Element layer : children(child(qgis,"projectlayers"),"maplayer")) {
            if (!isManagedObject(qgis,layer)) continue;
            if (object != null) return;  // A duplicated managed layer may be a user's alternate view.
            object = layer;
        }
        if (object == null || text(object,"id").isEmpty()) return;
        String id = text(object,"id");
        String oldName = text(object,"layername");
        child(object,"layername").setTextContent("현장객체");
        Element tree = child(qgis,"layer-tree-group");
        if (tree == null) return;
        NodeList nodes = tree.getElementsByTagName("layer-tree-layer");
        for (int i=0; i<nodes.getLength(); ++i) {
            Element node = (Element)nodes.item(i);
            if (id.equals(node.getAttribute("id")) && oldName.equals(node.getAttribute("name"))
                    && !"0".equals(node.getAttribute("useLayerName")))
                node.setAttribute("name","현장객체");
        }
    }

    private static Element append(Element parent, String tag) {
        Element item = parent.getOwnerDocument().createElement(tag);
        parent.appendChild(item);
        return item;
    }

    private static Element container(Element parent, String tag) {
        Element old = child(parent, tag);
        return old != null ? old : append(parent, tag);
    }

    private static Element match(Element parent, String tag, String attribute, String value) throws IOException {
        Element found = null;
        for (Element item : children(parent, tag)) {
            if (value.equals(item.getAttribute(attribute))) {
                if (found != null) throw new IOException("Duplicate project field " + value);
                found = item;
            }
        }
        return found;
    }

    private static boolean visibleInForm(Element form, String field) {
        NodeList elements = form.getElementsByTagName("attributeEditorField");
        for (int i = 0; i < elements.getLength(); ++i) {
            if (field.equals(((Element)elements.item(i)).getAttribute("name"))) return true;
        }
        return false;
    }

    private static ZipEntry projectEntry(List<? extends ZipEntry> entries) throws IOException {
        ZipEntry main = null;
        for (ZipEntry entry : entries) {
            String name = entry.getName();
            if (!entry.isDirectory() && !name.contains("/") && name.toLowerCase(Locale.ROOT).endsWith(".qgs")) {
                if (main != null) throw new IOException("Ambiguous QGS in project archive");
                main = entry;
            }
        }
        if (main == null) throw new IOException("Missing QGS in project archive");
        return main;
    }

    private static final Pattern ANDROID_FILES = Pattern.compile(
            "^/(?:storage/(?:emulated/[0-9]+|self/primary|[A-Za-z0-9_-]+)/Android/data|data/(?:user/[0-9]+|data))/kr\\.co\\.sungsan\\.mobilegis/files/");
    private static final String FIELD_STORE = "sungsan_field.gpkg";
    private static final String FIELD_TABLE = "|layername=sungsan_field_objects";

    // Return only actual files in this project. For relocated packages, recognize
    // the app's generated table AND its project/layer markers and original site/date.
    private static String packagedPath(Element project, Element layer, File dir, String uri) throws IOException {
        String provider = text(layer,"provider");
        if (!"ogr".equals(provider) && !"gdal".equals(provider)) return null;
        int split = uri.indexOf('|');
        String path = split < 0 ? uri : uri.substring(0,split);
        String absolute = path.startsWith("localized:") ? "/" + path.substring(10) : path;
        java.util.regex.Matcher app = ANDROID_FILES.matcher(absolute);
        if (!app.find() || absolute.contains("//")
                || absolute.contains("/../") || absolute.contains("/./") || absolute.contains("\\")) return null;
        File source = new File(absolute);
        // Check both the written and canonical paths, so a symlink cannot pull
        // a file outside the export tree into the mapping.
        if (source.getPath().startsWith(dir.getPath() + File.separator) && source.isFile()
                && source.getCanonicalPath().startsWith(dir.getPath() + File.separator)) {
            return "./" + source.getCanonicalPath().substring(dir.getPath().length() + 1).replace(File.separatorChar,'/');
        }
        String local = absolute.substring(app.end());
        String[] parts = local.split("/", -1);
        if (parts.length != 5 || !"Created Projects".equals(parts[0]) || !FIELD_STORE.equals(parts[4])
                || !"ogr".equals(provider) || split < 0 || !FIELD_TABLE.equals(uri.substring(split))) return null;
        Element config = child(child(project,"properties"),"SungsanMobileGIS");
        if (!"field-landstar".equals(text(config,"template"))
                || !"kr.co.sungsan.mobilegis.field-package/1".equals(text(config,"fieldPackage"))) return null;
        Element properties = child(child(layer,"customproperties"),"Option");
        Element marker = match(properties,"Option","name","kr.co.sungsan.mobilegis/fieldObjects");
        if (marker == null || !"true".equals(marker.getAttribute("value"))) return null;
        String date = text(config,"workDate");
        if (!date.matches("[0-9]{8}") || !parts[3].matches(Pattern.quote(date + "_" + parts[2]) + "(?:_[0-9]+)?")
                || !parts[1].equals(safePart(text(config,"regionName"),"지역"))
                || !parts[2].equals(safePart(text(config,"siteName"),project.getAttribute("projectname")))) return null;
        File bundled = new File(dir,FIELD_STORE);
        if (!bundled.isFile() || !bundled.getCanonicalFile().getParentFile().equals(dir))
            throw new IOException("Missing packaged field GeoPackage");
        return "./" + FIELD_STORE;
    }

    private static String safePart(String input, String fallback) {
        String s = input.trim();
        if (s.isEmpty()) s = fallback;
        s = s.replaceAll("[<>:\"/|?*\\\\]", "_").replace('\0','_');
        String base = s.contains(".") ? s.substring(0,s.lastIndexOf('.')) : s;
        if (base.matches("COM[0-9]|CON|LPT[0-9]|NUL|PRN|AUX|com[0-9]|con|lpt[0-9]|nul|prn|aux")) s = "_" + s;
        return s.endsWith(".") ? s.substring(0,s.length()-1) + "_" : s;
    }

    private static String[] repairLayerSources(Element project, File dir, boolean rewrite) throws IOException {
        Map<String,String> files = new LinkedHashMap<>();
        Element tree = child(project,"layer-tree-group");
        NodeList treeLayers = tree == null ? null : tree.getElementsByTagName("layer-tree-layer");
        for (Element layer : children(child(project,"projectlayers"),"maplayer")) {
            String uri = text(layer,"datasource");
            String relative = packagedPath(project,layer,dir,uri);
            if (relative == null) continue;
            int split = uri.indexOf('|');
            String path = split < 0 ? uri : uri.substring(0,split);
            String previous = files.put(path, new File(dir,relative).getCanonicalPath());
            if (previous != null && !previous.equals(files.get(path))) throw new IOException("Ambiguous project datasource");
            if (!rewrite) continue;
            String portable = relative + (split < 0 ? "" : uri.substring(split));
            child(layer,"datasource").setTextContent(portable);
            String id = text(layer,"id");
            for (int i=0; treeLayers != null && i<treeLayers.getLength(); ++i) {
                Element item = (Element)treeLayers.item(i);
                if (id.equals(item.getAttribute("id")) && uri.equals(item.getAttribute("source"))) item.setAttribute("source",portable);
            }
        }
        List<String> pairs = new ArrayList<>();
        for (Map.Entry<String,String> f : files.entrySet()) { pairs.add(f.getKey()); pairs.add(f.getValue()); }
        return pairs.toArray(new String[0]);
    }

    // Called before QGIS reads an old generated QGS/QGZ. The original stays untouched.
    static String[] mobileProjectSources(String projectPath) throws Exception {
        File source = new File(projectPath).getCanonicalFile();
        File dir = source.getParentFile();
        if (!new File(dir, FIELD_STORE).isFile()) return new String[0];
        if (source.getName().toLowerCase(Locale.ROOT).endsWith(".qgs")) {
            try (InputStream in = new BufferedInputStream(new FileInputStream(source))) {
                return repairLayerSources(parse(new InputSource(in)).getDocumentElement(),dir,false);
            }
        }
        try (ZipFile zip = new ZipFile(source)) {
            ZipEntry main = projectEntry(Collections.list(zip.entries()));
            try (InputStream in = zip.getInputStream(main)) {
                return repairLayerSources(parse(new InputSource(in)).getDocumentElement(),dir,false);
            }
        }
    }

    private static void patch(InputStream in, OutputStream out, Document spec, File dir) throws Exception {
        Document project = parse(new InputSource(in));
        Element qgis = project.getDocumentElement();
        if (!"qgis".equals(qgis.getTagName())) throw new IOException("Invalid QGIS project");
        Map<String,Element> pending = new HashMap<>();
        for (Element layer : children(spec.getDocumentElement(), "layer")) {
            String id = layer.getAttribute("id");
            if (id.isEmpty() || pending.put(id,layer) != null) throw new IOException("Invalid photo layer id");
        }
        if (pending.isEmpty() && !"1".equals(spec.getDocumentElement().getAttribute("sourcesOnly")))
            throw new IOException("Missing photo layer settings");
        repairLayerSources(qgis,dir,true);
        normalizeLegacyObjectName(qgis);
        for (Element layer : children(child(qgis,"projectlayers"), "maplayer")) {
            Element layerId = child(layer, "id");
            if (layerId == null) continue;
            Element updates = pending.remove(layerId.getTextContent());
            if (updates != null) patchFields(layer, updates);
        }
        if (!pending.isEmpty()) throw new IOException("Active photo layer not found in returned project");
        Transformer writer = TransformerFactory.newInstance().newTransformer();
        writer.setOutputProperty(OutputKeys.ENCODING, "UTF-8");
        DocumentType type = project.getDoctype();
        if (type != null && type.getSystemId() != null) writer.setOutputProperty(OutputKeys.DOCTYPE_SYSTEM, type.getSystemId());
        if (type != null && type.getPublicId() != null) writer.setOutputProperty(OutputKeys.DOCTYPE_PUBLIC, type.getPublicId());
        writer.transform(new DOMSource(project), new StreamResult(out));
    }

    private static void patchFields(Element layer, Element updates) throws Exception {
        Element configuration = container(layer,"fieldConfiguration");
        Element aliases = container(layer,"aliases");
        Element form = child(layer,"attributeEditorForm");
        Element layout = child(layer,"editorlayout");
        Set<String> fields = new HashSet<>();
        for (Element update : children(updates,"field")) {
            String name = update.getAttribute("name");
            String index = update.getAttribute("index");
            Element option = child(update,"Option");
            if (name.isEmpty() || !fields.add(name) || Integer.parseInt(index) < 0 || option == null || !"Map".equals(option.getAttribute("type")))
                throw new IOException("Invalid photo field in export");
            Element field = match(configuration, "field", "name", name);
            boolean newField = field == null && match(aliases, "alias", "field", name) == null;
            if (field == null) {
                field = append(configuration,"field");
                field.setAttribute("name", name);
                field.setAttribute("configurationFlags", "NoFlag");
            }
            Element widget = container(field, "editWidget");
            widget.setAttribute("type", "ExternalResource");
            Element config = container(widget, "config");
            Element original = child(config, "Option");
            Node replacement = layer.getOwnerDocument().importNode(option, true);
            if (original != null) config.replaceChild(replacement, original);
            else config.appendChild(replacement);
            Element alias = match(aliases, "alias", "field", name);
            if (alias == null) {
                alias = append(aliases,"alias");
                alias.setAttribute("field",name);
                alias.setAttribute("index",index);
            }
            if (alias.getAttribute("name").isEmpty()) alias.setAttribute("name", update.getAttribute("alias"));
            // Respect deliberately omitted legacy fields and the user's form layout.
            // Include a newly introduced slot alongside its siblings in a tab form.
            if (newField && form != null && layout != null && "tablayout".equals(layout.getTextContent())
                    && !visibleInForm(form, name)) {
                NodeList items = form.getElementsByTagName("attributeEditorField");
                Element parent = form;
                for (int i = 0; i < items.getLength(); ++i) {
                    Element sibling = (Element)items.item(i);
                    if (fields.contains(sibling.getAttribute("name"))) parent = (Element)sibling.getParentNode();
                }
                Element element = append(parent, "attributeEditorField");
                element.setAttribute("name", name);
                element.setAttribute("index", index);
                element.setAttribute("showLabel", "1");
            }
        }
        if (fields.size() != 4) throw new IOException("The return project must have four managed photo fields");
    }
}
