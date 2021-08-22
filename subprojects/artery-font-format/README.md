
# Artery Atlas Font format library

This is a header-only C++ library that facilitates encoding and decoding of the Artery Atlas Font format &ndash; a specialized binary file format for storing fonts as bitmap atlases used by the [Artery Engine](https://www.arteryengine.com/), intended for use in video games and other hardware accelerated applications.

An Artery Atlas font file (*.arfont) wraps together the atlas bitmap(s), which can be compressed e.g. in PNG format, the layout of the atlas, as well as the font's and the individual glyphs' metrics and positioning data, including kerning pairs.
