package ru.yandex.devtools.eval;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.Reader;
import java.io.Writer;
import java.util.Collection;
import java.util.Enumeration;
import java.util.InvalidPropertiesFormatException;
import java.util.Map;
import java.util.Properties;
import java.util.Set;

/**
 * @author Ilya Rubin
 */
public class RecurrentProperties extends Properties {
    private static final long serialVersionUID = 5263219869363443812L;

    private Properties backedProperties;

    public RecurrentProperties() {
        this(new Properties());
    }

    public RecurrentProperties(Properties backedProperties) {
        setBackedProperties(backedProperties);
    }

    public synchronized Object setProperty(String key, String value) {
        return backedProperties.setProperty(key, value);
    }

    public synchronized void load(Reader reader) throws IOException {
        throw new UnsupportedOperationException();
    }

    public synchronized void load(InputStream inStream) throws IOException {
        backedProperties.load(inStream);
    }

    @Deprecated
    public synchronized void save(OutputStream out, String comments) {
        backedProperties.save(out, comments);
    }

    public void store(Writer writer, String comments) throws IOException {
        throw new UnsupportedOperationException();
    }

    public void store(OutputStream out, String comments) throws IOException {
        backedProperties.store(out, comments);
    }

    public synchronized void loadFromXML(InputStream in) throws IOException,
            InvalidPropertiesFormatException
    {
        backedProperties.loadFromXML(in);
    }

    public synchronized void storeToXML(OutputStream os, String comment) throws IOException {
        backedProperties.storeToXML(os, comment);
    }

    public synchronized void storeToXML(OutputStream os, String comment, String encoding) throws
            IOException
    {
        backedProperties.storeToXML(os, comment, encoding);
    }

    public String getProperty(String key) {
        return EvalRecurrentProperties.getProperty(backedProperties, key);
    }

    public String getProperty(String key, String defaultValue) {
        return EvalRecurrentProperties.getProperty(backedProperties, key, defaultValue);
    }

    public Enumeration<?> propertyNames() {
        return backedProperties.propertyNames();
    }

    public Set<String> stringPropertyNames() {
        throw new UnsupportedOperationException();
    }

    public void list(PrintStream out) {
        backedProperties.list(out);
    }

    /*
    * Rather than use an anonymous inner class to share common code, this
    * method is duplicated in order to ensure that a non-1.1 compiler can
    * compile this file.
    */
    public void list(PrintWriter out) {
        backedProperties.list(out);
    }

    public int size() {
        return backedProperties.size();
    }

    public boolean isEmpty() {
        return backedProperties.isEmpty();
    }

    public Enumeration<Object> keys() {
        return backedProperties.keys();
    }

    public Enumeration<Object> elements() {
        return backedProperties.elements();
    }

    public boolean contains(Object value) {
        return backedProperties.contains(value);
    }

    public boolean containsValue(Object value) {
        return backedProperties.containsValue(value);
    }

    public boolean containsKey(Object key) {
        return backedProperties.containsKey(key);
    }

    public Object get(Object key) {
        if (key instanceof String)
            return getProperty((String) key);

        return backedProperties.get(key);
    }

    public Object put(Object key, Object value) {
        return backedProperties.put(key, value);
    }

    public Object remove(Object key) {
        return backedProperties.remove(key);
    }

    public void putAll(Map<?, ?> t) {
        backedProperties.putAll(t);
    }

    public void clear() {
        backedProperties.clear();
    }

    public Object clone() {
        RecurrentProperties clone = (RecurrentProperties) super.clone();
        clone.setBackedProperties((Properties) backedProperties.clone());
        return clone;
    }

    public String toString() {
        return backedProperties.toString();
    }

    public Set<Object> keySet() {
        return backedProperties.keySet();
    }

    public Set<java.util.Map.Entry<Object, Object>> entrySet() {
        return backedProperties.entrySet();
    }

    public Collection<Object> values() {
        return backedProperties.values();
    }

    public boolean equals(Object o) {
        if (o instanceof RecurrentProperties) {
            return backedProperties.equals(((RecurrentProperties) o).getBackedProperties());
        }

        return o instanceof Properties && backedProperties.equals(o);
    }

    public int hashCode() {
        return backedProperties.hashCode();
    }

    public Properties getBackedProperties() {
        return backedProperties;
    }

    protected void setBackedProperties(Properties backedProperties) {
        this.backedProperties = backedProperties;
    }
}

