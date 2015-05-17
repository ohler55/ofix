# encoding: UTF-8

require 'field'

class Spec

  def initialize(doc)
    @major = doc.attributes['major']
    @minor = doc.attributes['minor']
    @fields = {}
    puts "*** read in #{doc.attributes}"
    doc.nodes.each { |n|
      puts "*** read in #{n.name}"
    }

    #puts "*** fields: #{doc.locate('fields')}"

    # TBD build spec from XML

    doc.locate('fields').each { |n|
      f = Field.new(n)
      @fields[f.name] = f
    }
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
