# encoding: UTF-8

class Spec

  def initialize(doc)
    @major = doc.attributes['major']
    @minor = doc.attributes['minor']
    puts "*** read in #{doc.attributes}"
    doc.nodes.each { |n|
      puts "*** read in #{n.name}"
    }
    # TBD build spec from XML
  end

  def gen_c(f)
    f.write(%|
// This file is auto-generated from ref/FIX#{@major}#{@minor}.xml and should not be modified.

#include "ofix/tagspec.h"
#include "ofix/tagreq.h"
#include "ofix/msgspec.h"
#include "ofix/versionspec.h"

extern struct _ofixVersionSpec	fix#{@major}#{@minor}Spec;

|)
    # TBD tags
    # TBD messages
    # TBD spec struct

  end

end # Spec
