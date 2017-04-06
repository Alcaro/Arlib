SECTIONS {
  /* Ask for an address of zero, just to pick something. The kernel
   * won't care, we're ASLR compatible. SIZEOF_HEADERS is because only
   * complete pages can be loaded; if we ask for a page-aligned base
   * address, the linker adds a bunch of whitespace. */
  . = SIZEOF_HEADERS;
  
  /* No need to list the sections, they're all included by default. We
   * don't know nor care what the order is. */
  
  /* But since we have a script already, we might as well wipe these
   * too. The note/comment sections are just a waste of space, and .dyn*
   * and .gnu.hash are used only by the dynamic linker (which is, of
   * course, not available). */
  /DISCARD/ : {
    *(.comment .comment.* .note .note.*)
    *(.dynsym .dynstr .gnu.hash .dynamic .shstrtab)
  }
  /* shstrtab isn't discarded properly, and there's a pile of section
   * headers we don't need either. But I can't find any tools to
   * remove them, unless I make one myself, so I'll just keep them. They
   * waste about 256 bytes. */
}
