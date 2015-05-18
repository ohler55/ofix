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

    puts "*** fields are"
    doc.locate('fields')[0].nodes.each { |n|
      f = Field.new(n)
      @fields[f.name] = f
    }
    # TBD find related fields

  end

  def gen_c(f)
    f.write(%|
// This file is auto-generated from ref/FIX#{@major}#{@minor}.xml and should not be modified.

#include "ofix/tagspec.h"
#include "ofix/tagreq.h"
#include "ofix/msgspec.h"
#include "ofix/versionspec.h"

extern struct _ofixVersionSpec	fix#{@major}#{@minor}Spec;

// ----- Tags -----
static struct _ofixTagSpec	tags[] = {
|)

    @fields.values.sort_by { |x| x.tag }.each { |field|
      field.gen_c(f)
    }
    f.write(%|    { 0 }
};

// ----- Messages -----
|)

    # TBD messages
    # TBD spec struct

  end

end # Spec
