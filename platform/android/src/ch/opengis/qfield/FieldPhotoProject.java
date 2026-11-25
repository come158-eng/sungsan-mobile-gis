// SPDX-License-Identifier: GPL-2.0-or-later
package ch.opengis.qfield;

import java.io.*;
import java.util.*;
import java.util.zip.*;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.transform.*;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;
import org.w3c.dom.*;
import org.xml.sax.InputSource;

/** Apply only active field-photo settings to a cache copy of a legacy QGIS project. */
final class FieldPhotoProject {
    private FieldPhotoProject() {}

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
                patch(in, out, spec);
            }
            return;
        }
        if (!source.getName().toLowerCase(Locale.ROOT).endsWith(".qgz")) {
            throw new IOException("Unsupported desktop project");
        }
        try (ZipFile archive = new ZipFile(source);
             ZipOutputStream out = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(output)))) {
            List<? extends ZipEntry> entries = Collections.list(archive.entries());
            ZipEntry main = null;
            for (ZipEntry entry : entries) {
                String name = entry.getName();
                if (!entry.isDirectory() && !name.contains("/") && name.toLowerCase(Locale.ROOT).endsWith(".qgs")) {
                    if (main != null) throw new IOException("Ambiguous QGS in project archive");
                    main = entry;
                }
            }
            if (main == null) throw new IOException("Missing QGS in project archive");
            byte[] buffer = new byte[65536];
            for (ZipEntry entry : entries) {
                ZipEntry copied = new ZipEntry(entry.getName());
                copied.setTime(entry.getTime());
                copied.setComment(entry.getComment());
                out.putNextEntry(copied);
                CRC32 crc = new CRC32();
                try (CheckedInputStream in = new CheckedInputStream(archive.getInputStream(entry), crc)) {
                    if (entry == main) {
                        patch(in, out, spec);
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

    private static void patch(InputStream in, OutputStream out, Document spec) throws Exception {
        Document project = parse(new InputSource(in));
        Element qgis = project.getDocumentElement();
        if (!"qgis".equals(qgis.getTagName())) throw new IOException("Invalid QGIS project");
        Map<String,Element> pending = new HashMap<>();
        for (Element layer : children(spec.getDocumentElement(), "layer")) {
            String id = layer.getAttribute("id");
            if (id.isEmpty() || pending.put(id,layer) != null) throw new IOException("Invalid photo layer id");
        }
        if (pending.isEmpty()) throw new IOException("Missing photo layer settings");
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
